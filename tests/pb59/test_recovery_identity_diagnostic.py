from pathlib import Path
import subprocess
import tempfile

OLD = 'restored = now && *now == steps[index].move.source;'
NEW = '''restored = now && *now == steps[index].move.source &&
                        GetItemByPos(now->position) == steps[index].item;'''
FIXTURE = r'''
#include <array>
#include <cstdint>
#include <optional>
#include <iostream>
#include <stdexcept>
struct Facts { std::uint64_t guid; bool operator==(Facts const&) const = default; };
struct Slot { unsigned position; std::optional<Facts> item; bool operator==(Slot const&) const = default; };
struct Item {};
struct Move { Slot source; };
struct Step { Move move; Item* item; };
enum class Outcome { RecoveryRequired };
struct Result { Outcome outcome; std::size_t recoveryCount; std::array<std::uint64_t,19> recoveryGuids; };
struct Ledger { unsigned calls = 0; void RequireRecovery(int) { ++calls; } };
int main() {
  Item original, replacement;
  std::array<Step,2> steps{{{{{10,Facts{101}}}, &original}, {{{11,Facts{102}}}, &replacement}}};
  constexpr std::size_t count = 2;
  Result result{}; Ledger _pb59Transaction; int ticket = 0;
  bool alive = true, throws = false; unsigned captures = 0, pointers = 0;
  std::array<std::optional<Slot>,2> observed{steps[0].move.source,steps[1].move.source};
  std::array<Item*,2> actual{&original,&replacement};
  auto live = [&]{return alive;};
  auto CapturePB59InventorySlot = [&](unsigned pos) { ++captures; if (throws) throw std::runtime_error("capture"); return observed.at(pos-10); };
  [[maybe_unused]] auto GetItemByPos = [&](unsigned pos) { ++pointers; return actual.at(pos-10); };
  BODY
  unsigned checks = 0, failures = 0;
  auto expect = [&](bool value,const char* name){ ++checks; if (!value) {++failures; std::cerr << name << '\n';} };
  auto a = recoveryResult(); expect(a.outcome==Outcome::RecoveryRequired && a.recoveryCount==0,"both proved");
  actual[0]=&replacement; a=recoveryResult(); expect(a.recoveryCount==1 && a.recoveryGuids[0]==101,"same value different pointer");
  actual[0]=nullptr; a=recoveryResult(); expect(a.recoveryCount==1 && a.recoveryGuids[0]==101,"null pointer");
  actual[0]=&original; observed[0]=Slot{10,Facts{777}}; a=recoveryResult(); expect(a.recoveryCount==1 && a.recoveryGuids[0]==101,"changed value");
  observed[0].reset(); a=recoveryResult(); expect(a.recoveryCount==1 && a.recoveryGuids[0]==101,"absent observation");
  throws=true; a=recoveryResult(); expect(a.recoveryCount==2 && a.recoveryGuids[0]==101 && a.recoveryGuids[1]==102,"capture exception");
  throws=false; alive=false; auto before=captures; auto p=pointers; a=recoveryResult(); expect(a.recoveryCount==2 && a.recoveryGuids[0]==101 && a.recoveryGuids[1]==102 && captures==before && pointers==p,"dead owner no access");
  expect(_pb59Transaction.calls==6,"live recovery required only");
  std::cout << checks << " checks, " << failures << " failures\n"; return failures?1:0;
}
'''
if __name__ == '__main__':
  import argparse
  parser = argparse.ArgumentParser()
  parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[2] / 'src/server/game/Entities/Player/PlayerPB59Inventory.cpp')
  args = parser.parse_args()
  source = args.source.read_text()
  start = source.index('    auto recoveryResult = [&]()')
  end = source.index('\n    };', start) + len('\n    };')
  candidate = source[start:end]
  assert candidate.count(NEW) == 1, 'expected exact pointer proof'
  BASE = candidate.replace(NEW, OLD)
  for cxx in ['g++','clang++']:
    for sanitizer in (False,True):
      for patched in (False,True):
        body = BASE.replace(OLD,NEW) if patched else BASE
        with tempfile.TemporaryDirectory() as d:
          src=Path(d)/'test.cpp'; exe=Path(d)/'test'; src.write_text(FIXTURE.replace('BODY',body))
          flags=['-std=c++20','-Wall','-Wextra','-Werror','-O1']
          if sanitizer: flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
          subprocess.run([cxx,*flags,str(src),'-o',str(exe)],check=True)
          r=subprocess.run([str(exe)],capture_output=True,text=True)
          print(cxx, 'sanitized' if sanitizer else 'plain', 'patched' if patched else 'baseline',r.returncode,r.stdout.strip(),r.stderr.strip(),flush=True)
          assert r.returncode==(0 if patched else 1)
