# Third-party code

Vendored, unmodified single-file decoders used by the music player. Each folder keeps its upstream licence file.

| File | Version | Upstream | Commit (2026-10) | Licence |
|---|---|---|---|---|
| `dr_libs/dr_wav.h` | v0.14.6 | https://github.com/mackron/dr_libs | `bad753d5ade4f30e40fcf80a757210b650681f8f` | Public domain (Unlicense) or MIT-0 |
| `dr_libs/dr_mp3.h` | v0.7.4 | https://github.com/mackron/dr_libs | `bad753d5ade4f30e40fcf80a757210b650681f8f` | Public domain (Unlicense) or MIT-0 |
| `dr_libs/dr_flac.h` | v0.13.4 | https://github.com/mackron/dr_libs | `bad753d5ade4f30e40fcf80a757210b650681f8f` | Public domain (Unlicense) or MIT-0 |
| `stb/stb_vorbis.c` | v1.22 | https://github.com/nothings/stb | `003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5` | MIT or public domain |

SHA-256 of the vendored files:

```
ba2b6a7dd278bb11321e3bfe32975cbe080c5b38f249dcc4a239b8650b5e0f97  dr_libs/dr_wav.h
390e4e0ef0cff80fb86ebf1b461d6947cfa4ed699a4fe107e5a5e69a47db2950  dr_libs/dr_mp3.h
b0bf397159f212676454ac9dc92b515adf5d535750bc7a9b122d9a8101ddd100  dr_libs/dr_flac.h
dd1c647e6f767f8ff4b2dfae0fed314726600a01e0cf1ef556afddd5fa96ff15  dr_libs/LICENSE
4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636  stb/stb_vorbis.c
bebfe904b14301657e4e5d655c811d51fd31b97c455b9cc2d8600d6bac6cff63  stb/LICENSE
```

Do not edit these files. Compile each implementation once, in its own translation unit, with warnings relaxed for that file only.
