# Changelog

## 0.1.0 — initial extraction

- Extract the maintained uNodeArtNet Art-Net / ArtRdm implementation.
- Remove Arduino dependencies from the C++11 protocol core.
- Inject UDP transport, monotonic clock, and bounded random source.
- Add Arduino adapters for WiFiUDP and EthernetUDP; refresh all four examples.
- Add independent CMake tests and Linux/Windows/Arduino CI, including sanitizers.
- Guard receive recovery after partial reads and discard old delayed replies on failed rebinds.
- Retain upstream attribution and GPL-2.0-only licensing.

sACN, USB networking drivers, and multi-interface orchestration are not included.
