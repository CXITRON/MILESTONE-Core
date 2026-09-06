#include <MilestoneV5Manifest.h>
#include <cassert>
#include <string>
bool parse(const std::string &text){MilestoneV5::SignedImageManifest m{};return MilestoneV5::decodeImageManifest(reinterpret_cast<const uint8_t*>(text.data()),text.size(),m);}
int main(){
  const std::string hash(64,'a');
  assert(parse("MILESTONE-V5 MAIN 5.0.0 123456 "+hash+" 1 1\n"));
  assert(parse("MILESTONE-V5 ZERO 5.1.2 4294967295 "+hash+" 1 255\n"));
  assert(!parse("MILESTONE-V5 ZERO 5.1.2 1 "+hash+" 2 1\n"));
  assert(!parse("MILESTONE-V5 ZERO 5.1.2 1 "+hash+" 0 1\n"));
  assert(!parse("MILESTONE-V5 ZERO 5.1.2 1 "+hash+" 1 256\n"));
  assert(!parse("MILESTONE-V5 MAIN 5.0.0 4294967296 "+hash+" 1 1\n"));
  assert(!parse("MILESTONE-V5 MAIN 65536.0.0 1 "+hash+" 1 1\n"));
  assert(!parse("MILESTONE-V5 MAIN 05.0.0 1 "+hash+" 1 1\n"));
  assert(!parse("MILESTONE-V5 MAIN 5.0.0 01 "+hash+" 1 1\n"));
  assert(!parse("MILESTONE-V5 MAIN 5.0.0 0 "+hash+" 1 1\n"));
  assert(!parse("MILESTONE-V5 MAIN 5.0.0 +1 "+hash+" 1 1\n"));
  assert(!parse("MILESTONE-V5 MAIN 5.0.0 1 "+hash+" 1 1"));
  assert(!parse("MILESTONE-V5 MAIN 5.0.0 1 "+hash+" 1 1\n "));
  assert(!parse("MILESTONE-V5 MAIN 5.0.0 1 "+std::string(64,'A')+" 1 1\n"));
  assert(!parse(std::string(500,'x')));
}
