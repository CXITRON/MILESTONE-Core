#include "../v5/MilestoneV5Zero/V5OtaReceiver.h"
#include <cassert>
#include <string>
#include <vector>
using State=MilestoneV5::RemoteOtaState;
const std::vector<uint8_t> image={1,2,3,4,5,6,7,8,9,10,11,12};
std::string manifest(const char *target="ZERO"){
  mbedtls_sha256_context hash;mbedtls_sha256_init(&hash);mbedtls_sha256_update(&hash,image.data(),image.size());uint8_t digest[32];mbedtls_sha256_finish(&hash,digest);const char *hex="0123456789abcdef";std::string text;
  for(auto b:digest){text+=hex[b>>4];text+=hex[b&15];}
  return std::string("MILESTONE-V5 ")+target+" 5.0.0 12 "+text+" 1 1\n";
}
std::vector<uint8_t> command(uint8_t op){std::vector<uint8_t> p(5);p[0]=op;MilestoneV5::otaPut32(p.data()+1,123);return p;}
std::vector<uint8_t> chunk(uint8_t op,uint32_t offset,const uint8_t *data,size_t n){auto p=command(op);p.resize(9+n);MilestoneV5::otaPut32(p.data()+5,offset);memcpy(p.data()+9,data,n);return p;}
State send(V5OtaReceiver &receiver,const std::vector<uint8_t> &p){uint8_t out[10];size_t n=0;assert(receiver.request(p.data(),p.size(),out,n));assert(n==10);assert(MilestoneV5::otaU32(out+1)==123);return static_cast<State>(out[5]);}
void prepare(V5OtaReceiver &receiver,const char *target="ZERO",uint8_t signature=42){
  std::string text=manifest(target);auto start=command(1);start.resize(9);start[5]=text.size();start[6]=text.size()>>8;start[7]=1;
  assert(send(receiver,start)==State::Manifest);std::vector<uint8_t> metadata(text.begin(),text.end());metadata.push_back(signature);
  auto first=chunk(2,0,metadata.data(),50);assert(send(receiver,first)==State::Manifest);assert(send(receiver,first)==State::Manifest);assert(receiver.received==50);
  send(receiver,chunk(2,50,metadata.data()+50,metadata.size()-50));
}
void reset(){FakeOta::reset();FakeNvs::records.clear();FakeNvs::writeFailure=false;}
int main(){
  reset();{
    V5OtaReceiver receiver;prepare(receiver);assert(receiver.state==State::Quiescing);assert(FakeOta::beginCalls==0);
    receiver.service(false,true,millis());assert(FakeOta::beginCalls==0);receiver.service(true,true,millis());assert(receiver.state==State::Writing);
    auto first=chunk(3,0,image.data(),6);assert(send(receiver,first)==State::Writing);assert(send(receiver,first)==State::Writing);assert(FakeOta::writeCalls==1);
    send(receiver,chunk(3,6,image.data()+6,6));assert(send(receiver,command(4))==State::Ready);assert(FakeOta::selected==0);
    assert(send(receiver,command(5))==State::Rebooting);assert(FakeOta::selected==0x20000);
    assert(send(receiver,command(6))==State::Failed);FakeOta::running=1;FakeOta::bootState=ESP_OTA_IMG_PENDING_VERIFY;
    assert(send(receiver,command(6))==State::BootTesting);FakeOta::bootState=ESP_OTA_IMG_VALID;assert(send(receiver,command(6))==State::Complete);
    FakeOta::identities[1][0]^=1;assert(send(receiver,command(6))==State::Failed);FakeOta::identities[1][0]^=1;
    FakeOta::running=0;assert(send(receiver,command(6))==State::Failed);
  }
  reset();{V5OtaReceiver receiver;prepare(receiver,"MAIN");assert(receiver.state==State::Failed);assert(FakeOta::beginCalls==0);}
  reset();{V5OtaReceiver receiver;prepare(receiver,"ZERO",0);assert(receiver.state==State::Failed);assert(FakeOta::beginCalls==0);}
  reset();{V5OtaReceiver receiver;prepare(receiver);receiver.service(true,true,millis());send(receiver,chunk(3,1,image.data(),6));assert(receiver.state==State::Failed);assert(FakeOta::abortCalls==1);assert(FakeOta::selected==0);}
  reset();{V5OtaReceiver receiver;prepare(receiver);receiver.service(true,true,millis());auto bad=image;bad[0]^=1;send(receiver,chunk(3,0,bad.data(),bad.size()));send(receiver,command(4));assert(receiver.state==State::Failed);assert(FakeOta::endCalls==0);assert(FakeOta::selected==0);}
  reset();{V5OtaReceiver receiver;prepare(receiver);receiver.service(true,true,millis());FakeOta::writeFailure=true;send(receiver,chunk(3,0,image.data(),image.size()));assert(receiver.state==State::Failed);assert(FakeOta::abortCalls==1);}
  reset();{V5OtaReceiver receiver;prepare(receiver);receiver.service(true,true,millis());receiver.service(true,false,millis());assert(receiver.state==State::Failed);assert(FakeOta::abortCalls==1);}
  reset();{V5OtaReceiver receiver;prepare(receiver);receiver.service(true,true,millis());FakeOta::now+=15001;receiver.service(true,true,millis());assert(receiver.state==State::Failed);assert(FakeOta::selected==0);}
  reset();{V5OtaReceiver receiver;prepare(receiver);receiver.service(true,true,millis());send(receiver,chunk(3,0,image.data(),image.size()));send(receiver,command(4));FakeNvs::writeFailure=true;send(receiver,command(5));assert(receiver.state==State::Failed);assert(FakeOta::selected==0);}
}
