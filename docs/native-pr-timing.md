# What the shared C++ checks cost on a normal PR

On November 22, 2026, we ran the same [two-line Pole Field test change](https://github.com/seandearnaley/human-being-audio/pull/25/files) against the previous studio pipeline and the shared LLVM pipeline. It adds a 257-frame host buffer to a sample-for-sample DSP comparison. Both runs passed on GitHub-hosted runners.

| Measurement | Before | After | Difference |
| --- | ---: | ---: | --- |
| Time until the last Actions check finished | 15m 33s | 10m 43s | 4m 50s shorter (31.1%) |
| Sum of elapsed job times | 61m 35s | 27m 09s | 34m 26s shorter (55.9%) |

Wall time starts when GitHub creates the PR workflow and ends when its last executed job finishes. The job sum counts concurrent jobs independently. It excludes skipped jobs, queue waits before a job starts, Vercel deployments and billing multipliers. This is one paired observation. Runner load and cache state can change subsequent results.

| Quality work on this PR | Elapsed |
| --- | ---: |
| [Format job](https://github.com/seandearnaley/human-being-audio/actions/runs/45072031923/job/137360977272) | 0m 10s |
| [clang-tidy step inside the Ubuntu build](https://github.com/seandearnaley/human-being-audio/actions/runs/45072031923/job/137360977326) | 0m 03s |
| [Ubuntu RealtimeSanitizer job, including setup, build and tests](https://github.com/seandearnaley/human-being-audio/actions/runs/45072031923/job/137360977435) | 1m 53s |

The format and sanitizer jobs run alongside the platform builds. clang-tidy reuses the normal Ubuntu build's compilation database. The selector runs the affected plugin and still runs every plugin for shared native changes. This comparison includes the effect of selecting jobs as well as the LLVM work.

Before runs at `c00d33b9b4`. [Actions run 45067344066](https://github.com/seandearnaley/human-being-audio/actions/runs/45067344066) · [Actions run 45067344051](https://github.com/seandearnaley/human-being-audio/actions/runs/45067344051)

After run at `07b302e495`. [Actions run 45072031923](https://github.com/seandearnaley/human-being-audio/actions/runs/45072031923)
