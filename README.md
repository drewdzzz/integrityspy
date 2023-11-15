# integrityspy

## Project description

Integrity spy allows you to check integrity of a directory in background.
It saves checksum on the start, then it wakes up every `interval` seconds
and calculates them again. Also, the check can be triggerred with SIGUSR1.
Use SIGTERM to gracefully shutdown the demon.

The parameters can be set with `--dir` (`-d`) and `--interval` (`-n`) command
line arguments or `dir` and `interval` env variables.

The project is UNIX-compatible in theory, but is tested only on macos and ubuntu.
If the project is built on Linux, it will use `inotify` to start a check on
each directory modification.

Results of integrity check are written to syslog (`OK` or `FAIL`), report is
saved as a JSON object to `.integrityspy-report.json` in current workdir.

The demon handles only regular files and skips hidden ones.

## Build & run

In the source tree:

```bash
mkdir build
cd build
cmake ..
make -j
./src/integrityspy --dir ./my_dir --interval 3600
```

## Test

In the source tree:

```bash
cd test
python3 integrityspy.py
```