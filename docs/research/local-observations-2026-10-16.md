# Local observations (2026-10-16, macOS 26.5 build 25F71, Apple clang 21, CMake 4.3.3, no full Xcode — CLT only)

## Bonjour on the LAN
_raop._tcp:
  0A8408D21F78@Gabriel’s MacBook Pro  -> port 7000 (macOS built-in AirPlay Receiver)
     cn=0,1,2,3 da=true et=0,3,5 ft=0x4A7FCFD5,0x38174FDE sf=0x204 md=0,1,2 am=Mac17,8 pk=<ed25519> tp=UDP vn=65537 vs=950.7.1 vv=0
  cbf2b4c42f7c@auxCord                -> port 53108 (dynamic), auxCord 2.6.1.38 running
     am=MntRd,1 ch=2 cn=0,1 da=true ek=1 et=0,1 md=0,1,2 pw=false sm=false sr=44100 ss=16 sv=false tp=UDP txtvers=1 vn=3 vs=130.14
_airplay._tcp: only the Mac (port 7000) and a Roku. auxCord does NOT advertise _airplay._tcp => pure AirPlay 1 / RAOP receiver.
Listening ports on the Mac: 5000 and 7000 (tcp4+tcp6) = built-in receiver. Coexistence with auxCord on 53108 observed live.

## auxCord bundle (/Applications/auxCord.app)
- CFBundleIdentifier com.yourcompany.auxCordStandalone (Projucer default prefix + JUCE Standalone wrapper naming)
- LSMinimumSystemVersion 10.13; built with Xcode 15.3 / SDK macOS 14.4; archs x86_64+arm64; no entitlements; Team 3Y6DLW6UXM
- Links only system frameworks (CoreAudioKit, DiscRecording, WebKit, Metal, CoreMIDI, AudioToolbox, Security, Accelerate...) = classic JUCE link set
- Strings: "JUCE Version", "JUCE IPC server", OpenSSL 3.1.3 (statically linked, "nssl-3.1.3"), ALAC decoder ("Failed to initialse ALAC Decoder"), spdlog-style log format, SQLite
- Source layout leaked via asserts: source/airplay/{AirplayServer, AirplayClientSession, RtspConnection, RtpReceiver, DacpBrowserController, DacpClient}.cpp, source/network/{TcpListener,TcpConnection}.cpp, source/plugin/PluginProcessor.cpp, source/standalone/StandaloneApplication.cpp, source/gui/...
- Log strings: "Added RTSP Connection for DACP-ID \"{}\", this client session now has {} RTSP connections", "DACP-ID of the RTSP connection ({}) doesn't match our DACP-ID ({}). Dropping connection.", "Added DacpClient for DACP-ID {}", "http://{}:{}/ctrl-int/1/{}", "Invalid timing port number ({})", "Couldn't change sample rate", "Session active time/name/title"
  => auxCord groups RTSP connections into a *client session keyed by DACP-ID*; DACP via HTTP ctrl-int/1; has a PluginProcessor in the same JUCE project (so its plugin build likely runs the network stack in-process — contradicts our "plugin has no network" hypothesis; verify what the product actually ships).
