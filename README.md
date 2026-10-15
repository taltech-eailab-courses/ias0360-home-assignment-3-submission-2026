Home Assignment 3

This repository is the workspace for **Home Assignment 3**.

## Choose your assignment folder

- **IMU:** Students who selected IMU should work in `imu/`.
- **LCD (Image):** Students who selected LCD (Image) should work in `lcd/`.

## Before you start

The pico-tflmicro library is already configured as a Git submodule in both `imu/lib/pico-tflmicro/` and `lcd/lib/pico-tflmicro/`. Before starting the assignment, download and initialize the submodules.

From the directory containing `ias0360-home-assignment-3-submission-2026/`, run:

```sh
cd ias0360-home-assignment-3-submission-2026
git submodule update --init --recursive
```

Your workspace should then have the following structure:

```text
ias0360-home-assignment-3-submission-2026/
├── imu/
│   └── lib/
│       └── pico-tflmicro/
├── lcd/
│   └── lib/
│       └── pico-tflmicro/
├── .gitmodules
└── README.md
```

The submodule URLs use SSH (`git@github.com`), so ensure your SSH key is configured for your GitHub account before running the command.
