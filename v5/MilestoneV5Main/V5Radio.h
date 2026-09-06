#pragma once
#include "V5Artwork.h"
#include "V5Hardware.h"
#include "V5Portal.h"
#include <MilestoneV5ArtworkWorker.h>
#include <MilestoneV5DownloadWorker.h>
#include <MilestoneV5Runtime.h>
#include <MilestoneV5WifiStore.h>
#include <esp_sntp.h>

namespace V5MainTime {
std::atomic<bool> received{false};
void synchronized(struct timeval *) { received.store(true); }
} // namespace V5MainTime

// MAIN owns Wi-Fi only for a bounded lease. A portal with a connected station
// is never closed; an idle portal is restored after the lease finishes.
class V5Radio {
public:
  bool busy = false, redraw = false, requestPortal = false;
  bool zeroArtworkAllowed = false;
  bool downloadWanted = false, downloadReady = false;
  void service(uint32_t now, V5Portal &portal, V5Hardware &hardware,
               V5Artwork &art, bool zeroOnline, uint16_t zeroFlags,
               bool safety) {
    const bool client = portal.active && !portal.canYieldRadio(now);
    MilestoneV5::RadioState state{portal.active,
                                  client,
                                  !portal.active,
                                  zeroOnline && bool(zeroFlags & 4),
                                  zeroOnline && bool(zeroFlags & 8),
                                  safety};
    zeroArtworkAllowed = MilestoneV5::assignNetworkTask(
                             art.manual ? MilestoneV5::TaskKind::kUserHttp
                                        : MilestoneV5::TaskKind::kArtwork,
                             state)
                             .board == MilestoneV5::Board::kZero;
    if (phase == 3) {
      bool connected =
          WiFi.status() == WL_CONNECTED && uint32_t(WiFi.localIP()) != 0;
      if (connected && !testConnected)
        testConnected = now;
      if (!connected)
        testConnected = 0;
      if (safety || requestPortal || now - started >= 15000 ||
          (testConnected && now - testConnected >= 2000)) {
        MilestoneV5::WifiStore store;
        bool ok = !safety && !requestPortal && testConnected &&
                  now - testConnected >= 2000 && store.save(portal.wifi);
        portal.wifiPending = false;
        portal.wifiReplicate = ok;
        portal.wifiResult = ok ? "MAIN 연결 저장 완료. ZERO 복귀 후 동기화"
                               : "MAIN 연결 시험 실패. 기존 네트워크 유지";
        portal.note(9, ok ? 0 : 1);
        WiFi.disconnect(false, false);
        WiFi.mode(portal.active ? WIFI_AP : WIFI_OFF);
        busy = false;
        phase = 0;
        requestPortal = false;
        redraw = true;
      }
      return;
    }
    if (!busy) {
      if (portal.wifiPending && !zeroOnline && !safety) {
        WiFi.mode(portal.active ? WIFI_AP_STA : WIFI_STA);
        MilestoneV5::connectWifi(portal.wifi);
        busy = true;
        phase = 3;
        started = now;
        testConnected = 0;
        redraw = true;
        return;
      }
      if (safety || (client && !portal.timeSyncRequested) ||
          (portal.wifiPending && zeroOnline) ||
          int32_t(now - retryAfter) < 0)
        return;
      bool wantsArt = art.stage == 1 && !zeroArtworkAllowed;
      bool wantsTime =
          (portal.timeSyncRequested || (!lastTimeSync && portal.system.bootSync) ||
           (portal.system.ntpSeconds &&
            now - lastTimeSync >= portal.system.ntpSeconds * 1000)) &&
          (!zeroOnline || state.zeroBleActive);
      if (!wantsArt && !wantsTime && !downloadWanted)
        return;
      MilestoneV5::WifiCredentials credentials;
      MilestoneV5::WifiStore store;
      if (!store.load(credentials, networkIndex)) {
        networkIndex = 0;
        if (!store.load(credentials)) {
          if (portal.timeSyncRequested) {
            portal.timeSyncRequested = false;
            portal.timeSyncSuccess = false;
          }
          retryAfter = now + portal.system.retrySeconds * 1000;
          return;
        }
      }
      if (++leaseSequence == 0)
        ++leaseSequence;
      if (!lease.acquire(leaseSequence,
                         wantsArt ? MilestoneV5::TaskKind::kArtwork
                                  : MilestoneV5::TaskKind::kNtp,
                         MilestoneV5::Board::kMain, now, 45000))
        return;
      restorePortal = portal.active;
      portal.close();
      WiFi.mode(WIFI_STA);
      WiFi.setSleep(portal.system.wifiSleep);
      MilestoneV5::connectWifi(credentials);
      busy = true;
      cancelled = false;
      redraw = true;
      phase = 1;
      started = now;
      generation = art.generation;
      downloadJob = downloadWanted;
      artJob = wantsArt && !downloadJob;
      return;
    }
    if (downloadJob && downloadWanted && now - started < 900000)
      lease.renew(leaseSequence, now, 45000);
    if (safety || requestPortal || lease.expireIfDue(now) ||
        (artJob && generation != art.generation) ||
        (downloadJob && !downloadWanted))
      cancelled = true;
    if (cancelled) {
      V5ArtworkWorker::cancel.store(true);
      V5DownloadWorker::cancel.store(true);
      downloadReady = false;
      if (V5ArtworkWorker::state.load(std::memory_order_acquire) != 1 &&
          V5DownloadWorker::state.load(std::memory_order_acquire) != 1) {
        if (artJob) {
          uint8_t response[6] = {4};
          for (unsigned i = 0; i < 4; ++i)
            response[i + 1] = generation >> (8 * i);
          response[5] = 2;
          art.result(response, 6);
        }
        finish(now, portal, safety);
      }
      return;
    }
    if (phase == 1) {
      if (WiFi.status() != WL_CONNECTED) {
        if (now - started >= 12000) {
          cancelled = true;
          ++networkIndex;
          retryAfter = now + portal.system.retrySeconds * 1000;
        }
        return;
      }
      V5MainTime::received.store(false);
      esp_sntp_set_time_sync_notification_cb(V5MainTime::synchronized);
      configTime(0, 0, "time.cloudflare.com", "time.google.com",
                 "pool.ntp.org");
      syncStarted = now;
      if (artJob) {
        uint8_t bytes[476];
        size_t n;
        if (!art.request(bytes, sizeof(bytes), n) || n < 6 || bytes[0] != 3 ||
            !V5ArtworkWorker::start(bytes + 5, n - 5)) {
          cancelled = true;
          retryAfter = now + 60000;
          return;
        }
        uint8_t response[6] = {3};
        for (unsigned i = 0; i < 4; ++i)
          response[i + 1] = generation >> (8 * i);
        art.result(response, sizeof(response));
      }
      phase = 2;
    }
    if (V5MainTime::received.exchange(false)) {
      time_t epoch = time(nullptr);
      if (epoch >= 1704067200 && epoch <= 4102444799LL &&
          hardware.setRtcEpoch(epoch)) {
        lastTimeSync = now;
        if (portal.timeSyncRequested) {
          portal.timeSyncRequested = false;
          portal.timeSyncSuccess = true;
        }
      }
      esp_sntp_stop();
    }
    if (downloadJob) {
      downloadReady = time(nullptr) >= 1704067200;
      if (!downloadReady && now - syncStarted >= 21000)
        cancelled = true;
      return;
    }
    if (!artJob) {
      if (lastTimeSync == now || now - syncStarted >= 21000) {
        if (lastTimeSync != now) {
          if (portal.timeSyncRequested) {
            portal.timeSyncRequested = false;
            portal.timeSyncSuccess = false;
          }
          retryAfter = now + 60000;
        }
        finish(now, portal, safety);
      }
      return;
    }
    const int worker = V5ArtworkWorker::state.load(std::memory_order_acquire);
    if (worker == 1)
      return;
    if (worker != 2) {
      uint8_t response[6] = {4};
      for (unsigned i = 0; i < 4; ++i)
        response[i + 1] = generation >> (8 * i);
      response[5] = 2;
      art.result(response, 6);
      finish(now, portal, safety);
      return;
    }
    if (art.stage != 2) {
      finish(now, portal, safety);
      return;
    }
    uint8_t chunk[468];
    for (unsigned i = 0; i < 4; ++i) {
      chunk[i] = generation >> (8 * i);
      chunk[i + 4] = art.received >> (8 * i);
    }
    size_t take =
        min(size_t(460), size_t(MilestoneV5::kArtworkBytes - art.received));
    memcpy(chunk + 8, V5ArtworkWorker::packet + art.received, take);
    if (!art.chunk(chunk, take + 8)) {
      cancelled = true;
      return;
    }
    redraw = true;
    if (art.stage == 0)
      finish(now, portal, safety);
  }

private:
  MilestoneV5::TaskLeaseController lease;
  uint32_t leaseSequence = 0, started = 0, generation = 0, retryAfter = 0,
           syncStarted = 0, lastTimeSync = 0, testConnected = 0;
  uint8_t phase = 0, networkIndex = 0;
  bool restorePortal = false, artJob = false, cancelled = false,
       downloadJob = false;
  void finish(uint32_t now, V5Portal &portal, bool safety) {
    esp_sntp_stop();
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_OFF);
    busy = false;
    downloadReady = false;
    downloadJob = false;
    phase = 0;
    lease.release(lease.leaseId());
    if (!safety && (restorePortal || requestPortal))
      portal.open();
    requestPortal = false;
    restorePortal = false;
    redraw = true;
    if (int32_t(retryAfter - now) < 0)
      retryAfter = now + 1000;
  }
};
