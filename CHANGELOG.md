# Changelog

All notable changes to this project will be documented in this file.

## [0.2.0](https://github.com/Axertin/wolf/compare/wolf-v0.1.1...wolf-v0.2.0) (2026-01-19)


### Features

* add giveItem to API ([f6c34a4](https://github.com/Axertin/wolf/commit/f6c34a486a0b8666735a2755841afb21c54f6e85))
* add triggers for gameplay markers ([be32c07](https://github.com/Axertin/wolf/commit/be32c0781e5bc918bb5dc19f9960db777f9a181d))


### Bug Fixes

* add missing  &lt;string&gt; include in sdk ([902cbac](https://github.com/Axertin/wolf/commit/902cbaccdacb3fb341b4df8f7d4898d5425dc2af))
* add missing extern C to giveItem ([85f19a4](https://github.com/Axertin/wolf/commit/85f19a461dac484e592dd10f415bd9ded4093fb5))
* add missing include guards in SDK ([dd5d153](https://github.com/Axertin/wolf/commit/dd5d1538759665f9f969e5dcb3454657fe0eb854))
* added Proton memory flags to valid flags ([bbfc6dd](https://github.com/Axertin/wolf/commit/bbfc6ddece3e2fe11882f517738b38206c94a9cf))
* adds missing include for cross-compilation ([4409b9b](https://github.com/Axertin/wolf/commit/4409b9b69f9390dcdd6380eac4980be214cb57c0))
* change console keybind to hardware location ([3a348d4](https://github.com/Axertin/wolf/commit/3a348d4847c24dc9b28dd0affddef0747d38d37f))
* correct bit order in BitField monitors ([d7dd51d](https://github.com/Axertin/wolf/commit/d7dd51d6b89581c76ea2b5650e4408436e01c484))
* fix unknown monitors ([c57fffe](https://github.com/Axertin/wolf/commit/c57fffe7ea3ee7866c14ace21e628bc99b7972e0))
* properly forward mouse wheel to mod contexts ([09e9ce7](https://github.com/Axertin/wolf/commit/09e9ce743e9ab7e8badea15a70c39159fba0c46d))
* remove spam devdatafinder prints ([e88a7ad](https://github.com/Axertin/wolf/commit/e88a7adc3bc5512de4bf62a3e71e5d4e4f55a3eb))
* removed duplicate imgui input handling ([03c1b40](https://github.com/Axertin/wolf/commit/03c1b40b409e2c4bb1f9a6554d3bbd43b86a9574))
* resolve crash when changing resolutions ([8329469](https://github.com/Axertin/wolf/commit/8329469d26bd9000a8f57e3ba8a79374b24b788d))
* resolve font rescaling at non-1080p sizes ([8d0af2c](https://github.com/Axertin/wolf/commit/8d0af2c03c367a1387a41bf1cf6809317627ff5c))
* resolve GUI click-through ([#43](https://github.com/Axertin/wolf/issues/43)) ([fa8eceb](https://github.com/Axertin/wolf/commit/fa8eceb6b895181ff670b8780072e4beda26cefc))
* stop blocking OS window controls with imgui ([8ae0991](https://github.com/Axertin/wolf/commit/8ae0991d17e16a6c337ea19734d990c7c20b9490))
* use wolf context to read control key state ([93e1945](https://github.com/Axertin/wolf/commit/93e194550703d53d7d646e062d01041dbb5f124b))


### Miscellaneous Chores

* depricate onGameStart ([d5aea23](https://github.com/Axertin/wolf/commit/d5aea232003f41d81128bb69370e4cd172c34d34))
* remove .cache from git ([4d88b51](https://github.com/Axertin/wolf/commit/4d88b51b83983bd25f74005d961b5d66a7ddff3d))
* stop setting up resource hooks for the moment ([8ac1f73](https://github.com/Axertin/wolf/commit/8ac1f739a153b7304cdee5cad7e9ac629bf48230))
* symlink compile_commands into root dir ([c9b580c](https://github.com/Axertin/wolf/commit/c9b580cc343e57c9635c6bc6148d3c74a6deffae))


### Documentation

* add main menu flag ([c9684af](https://github.com/Axertin/wolf/commit/c9684af2da3c4b69ee6b1acc9a07a7262fd8e03a))

## [0.1.1](https://github.com/Axertin/wolf/compare/wolf-v0.1.0...wolf-v0.1.1) (2025-09-19)


### Bug Fixes

* method of prerel skipping touching changelog ([cb3af66](https://github.com/Axertin/wolf/commit/cb3af667e82f3a732f448224e1259038be638ce2))


### Code Refactoring

* split huge runtime_api into many files ([d6f7ca0](https://github.com/Axertin/wolf/commit/d6f7ca05ddb288036e254329a2273a4249aa3c66))


### Miscellaneous Chores

* clean changelog ([9b65193](https://github.com/Axertin/wolf/commit/9b65193f1033352938925b93e520c11828c26d66))
* cleanup wolf_runtime_api ([1b7076e](https://github.com/Axertin/wolf/commit/1b7076e805688df30fc1271041d757e90666ba37))
* make prerel stop writing changelog ([e6d67e4](https://github.com/Axertin/wolf/commit/e6d67e41bb551b637fa8fdc4fc21e3ff9af4df31))


### Build System and Dependencies

* only compile rc in Release builds ([8d063ff](https://github.com/Axertin/wolf/commit/8d063ff4c7ef5d29b1ecdcdbabfe5c757330399d))
* remove extra build from release action ([183351c](https://github.com/Axertin/wolf/commit/183351c03976abcc4b5b7c8d223d0dda764bb014))

## [0.1.0](https://github.com/Axertin/wolf/compare/wolf-v0.0.1...wolf-v0.1.0) (2025-09-17)

### Features

* add imgui version check ([6e8d238](https://github.com/Axertin/wolf/commit/6e8d238d303a3ae9816ef35cfa8ded9ea497274b))
* Add release system ([d77b440](https://github.com/Axertin/wolf/commit/d77b44008b780126d0c8eeb8c0ced01ca5dd042e))
* Add state flag header generation using devtools yamls as source ([#13](https://github.com/Axertin/wolf/issues/13)) ([8d59500](https://github.com/Axertin/wolf/commit/8d595005efbe9c293fda4f36c7b29fb47e331b9e))
* add support for blocking hook callbacks ([#19](https://github.com/Axertin/wolf/issues/19)) ([5e534d1](https://github.com/Axertin/wolf/commit/5e534d1e23ac71229db41f92e75b974cbe66de8b))
* Initial Baseline functionality ([e00bc38](https://github.com/Axertin/wolf/commit/e00bc38d047e0ab3e0d8268153c37dc27f278c4c))

### Bug Fixes

* bytewise bit order in bitfield monitors ([eb02b90](https://github.com/Axertin/wolf/commit/eb02b90e0ce67a026cb73226f833671ea7166ac1))

### Miscellaneous Chores

* Add CONTRIBUTING.md and update PR template ([6f11400](https://github.com/Axertin/wolf/commit/6f114009355cffc62b34df2e7f292d8fc6db4f4b))
* Add feat request issue template ([1d1e069](https://github.com/Axertin/wolf/commit/1d1e069fb08282f762d5d215ba598c187ddde1ee))
* Add metainfo to the loader executable ([434378e](https://github.com/Axertin/wolf/commit/434378ebaefeb67832351069e27c2f86047454ed))
* change console key to ~ ([4b85d71](https://github.com/Axertin/wolf/commit/4b85d7109a30fe6a5dd1c755cae73ed5b7fc15cc))
* Game state header generation now compliant with clang-format ([#14](https://github.com/Axertin/wolf/issues/14)) ([1326e7d](https://github.com/Axertin/wolf/commit/1326e7d57ac2bb4144e747b442d79c89c37653f0))

### Build System and Dependencies

* ignore chores w/ master scope in notes ([cc0ff0e](https://github.com/Axertin/wolf/commit/cc0ff0e1176814c8c8affa29cc207088d0b9d0ed))
* ignore prerelease commit msgs in changelog ([7151fa2](https://github.com/Axertin/wolf/commit/7151fa232f352760912077a7f23d95c315a02df8))
* make prerelease commits non-conventional ([c1ccc65](https://github.com/Axertin/wolf/commit/c1ccc655f627d1261c37651fed6c621e8ccab7fd))
* overwrite release title ([4094349](https://github.com/Axertin/wolf/commit/4094349a861fa6e99c81f07960281a57b0afdf0e))
* testing prev ([47af263](https://github.com/Axertin/wolf/commit/47af2636c05410bf218a86c6484146b4583884bf))
* try a different title ([313dd17](https://github.com/Axertin/wolf/commit/313dd173e301889f1069911a873570dc7a1a033d))

### Documentation

* touch readme for runtime ([bcf9638](https://github.com/Axertin/wolf/commit/bcf9638bb7ea191c2ce429def3a1701802995008))
