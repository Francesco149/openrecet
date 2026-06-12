{
  description = "OpenRecet — open-source reimplementation of Recettear (educational RE / preservation)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };

        # 32-bit mingw cross compiler — Recettear is 32-bit Win32 PE.
        mingw32 = pkgs.pkgsCross.mingw32.buildPackages;

        # Dear ImGui source (Trace Studio v3 native viewer — compiled into the
        # 32-bit viewer.exe with the Win32 + DX9 backends; ImGui is meant to be
        # vendored into the app, so we pin the upstream source via nixpkgs and
        # point the Makefile at $IMGUI_SRC rather than committing it to the repo).
        imguiSrc = pkgs.imgui.src;

        # Python environment for tooling (extractors, test harness, contact sheets).
        pythonEnv = pkgs.python3.withPackages (ps: with ps; [
          pillow            # image manipulation, contact sheets
          numpy             # frame diffing math
          scikit-image      # SSIM perceptual diff
          opencv4           # frame capture / template match
          pytest            # test runner
          pytest-xdist      # parallel test execution
          pyyaml            # test manifest format
          construct         # binary-format parser DSL — perfect for .dat archive RE
          rich              # nicer CLI output for tools
        ]);

      in {
        devShells.default = pkgs.mkShell {
          name = "openrecet-dev";

          packages = with pkgs; [
            # ── reverse engineering ───────────────────────────────────
            ghidra            # NSA decompiler, primary analysis tool
            radare2           # CLI disassembler
            rizin             # radare2 fork, alternative
            cutter            # rizin GUI
            # retdec          # disabled — currently fails to build in nixpkgs (capstone dep). Ghidra+rizin cover cross-check.
            imhex             # advanced hex editor with pattern language
            hexyl             # quick hex dumps in terminal
            bvi               # vi-style hex editor
            file              # identify file types
            binutils          # nm, objdump, strings, readelf
            icoutils          # wrestool/icotool — extract resources from PE

            # ── dynamic analysis / instrumentation ────────────────────
            frida-tools       # hook DirectX calls (under Windows via WSLInterop)
            nodejs            # `node --check` for the Frida agent JS (tools/frida/*.js)
            # Wine intentionally NOT in the flake — modern nixpkgs wine for
            # 32-bit Win32 PE either builds from source (slow, fragile) or
            # uses the new wow64 mode that skips the 32-bit syswow64/ layer
            # and fails to load kernel32.dll for 32-bit exes. WSLInterop runs
            # the exe natively on the host Windows, which is reliable and
            # zero-setup. See docs/PLAN.md §6 for the rationale.

            # ── build toolchain (32-bit Win32 target) ─────────────────
            mingw32.gcc       # i686-w64-mingw32-gcc — produces Win32 PE
            mingw32.binutils
            gnumake
            cmake
            ninja
            pkg-config

            # ── steamless / .NET ──────────────────────────────────────
            mono              # fallback for Steamless.CLI.exe if WSLInterop unused
            dotnet-sdk_8      # modern dotnet for any port/tooling work

            # ── headless testing ──────────────────────────────────────
            # No xvfb / scrot — the exe runs via WSLInterop on the Windows
            # desktop. Frame capture happens inside the exe itself (when
            # invoked with --capture-to <dir>) — see src/main.c.
            ffmpeg            # frame extraction from any video captures

            # ── image/asset processing ────────────────────────────────
            imagemagick       # montage for contact sheets, conversion
            pngquant
            optipng

            # ── python environment ────────────────────────────────────
            pythonEnv

            # ── docs / reporting ──────────────────────────────────────
            pandoc            # for future auto-generated reports

            # ── general dev ────────────────────────────────────────────
            git
            git-lfs
            jq
            ripgrep
            fd
            bat
            tree
          ];

          # Environment hints for tooling.
          shellHook = ''
            export OPENRECET_ROOT=$PWD
            export OPENRECET_GAME_DIR="/mnt/c/Program Files (x86)/Steam/steamapps/common/Recettear"
            export OPENRECET_STEAMLESS_DIR="/mnt/c/Users/headpats/Documents/_devtools/Steamless.v3.1.0.5.-.by.atom0s"

            # mingw cross-compiler convenience aliases
            export MINGW_CC=i686-w64-mingw32-gcc
            export MINGW_AR=i686-w64-mingw32-ar
            export MINGW_STRIP=i686-w64-mingw32-strip

            # Dear ImGui source for the Trace Studio v3 native viewer (tools/trace_studio_v3/viewer).
            export IMGUI_SRC=${imguiSrc}

            # Banner only for an interactive shell (stdout is a tty). Under
            # `nix develop --command <cmd>` stdout is a pipe, so this stays silent
            # — otherwise the banner pollutes every tool's stdout (broke command
            # substitution + heredocs in capture scripts).
            if [ -t 1 ]; then
              echo "openrecet dev shell ready"
              echo "  game dir:    $OPENRECET_GAME_DIR"
              echo "  steamless:   $OPENRECET_STEAMLESS_DIR"
              echo "  mingw cc:    $(command -v $MINGW_CC || echo '(missing)')"
              echo "  exe runs via WSLInterop (no wine)"
              echo ""
              echo "Bootstrap: ./tools/setup.sh"
            fi
          '';
        };

        # Lean shell for CI (the nightly build). Just the mingw toolchain,
        # make, and a base python3 for the no-proprietary-bytes gate — none
        # of the heavy RE/analysis closure (ghidra, dotnet, frida, imhex,
        # opencv) the default shell pulls in. The build is asset-free
        # (no embedded SE; runtime-extracted — docs/formats/se-pack.md), so
        # CI needs no game files. See .github/workflows/nightly.yml.
        devShells.ci = pkgs.mkShell {
          name = "openrecet-ci";
          packages = [
            mingw32.gcc
            mingw32.binutils
            pkgs.gnumake
            pkgs.python3      # stdlib only — for tools/ci/no_proprietary_bytes.py
            pkgs.coreutils
          ];
        };

        # Package output: the openrecet.exe binary cross-compiled with mingw32.
        # Stub — wired up properly once src/ has a buildable program.
        packages.openrecet = pkgs.stdenv.mkDerivation {
          pname = "openrecet";
          version = "0.0.0-dev";
          src = ./src;

          nativeBuildInputs = [ mingw32.gcc mingw32.binutils pkgs.gnumake ];

          # Placeholder build — replaced once src/Makefile exists.
          buildPhase = ''
            echo "openrecet build stub — src/ not yet populated"
            mkdir -p $out/bin
            touch $out/bin/openrecet.exe
          '';

          installPhase = "true";

          meta = with pkgs.lib; {
            description = "Open-source drop-in for recettear.exe (educational/preservation)";
            license = licenses.mit;
            platforms = [ "x86_64-linux" ];
          };
        };
      });
}
