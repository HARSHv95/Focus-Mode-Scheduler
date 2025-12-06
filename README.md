# focusctl - Process CPU Scheduling via cgroups v2

A Linux utility for managing process CPU scheduling using cgroups v2. This project provides two main tools:

1. **focusctl** - Command-line tool for manual process prioritization
2. **focusd** - Daemon implementing lottery-based CPU scheduling

## Features

- **Manual Focus Management**: Move processes between focus (high priority) and background (low priority) groups
- **Lottery Scheduling**: User-level daemon that randomly selects processes based on ticket allocation
- **Process Matching**: Find and manage processes by name substring
- **Pomodoro Support**: Boost specific processes for a set duration
- **cgroups v2 Integration**: Uses modern cgroup v2 CPU weight controller
- **Easy Installation**: Automated installer and uninstaller scripts

## Requirements

- Linux kernel with cgroups v2 support
- Root privileges (sudo) - **Required for all operations**
- GCC or compatible C compiler
- Bash shell

## Project Structure

```
/home/bermuda/CS310/Project/
├── focusctl.c        # Manual process prioritization tool
├── focusd.c          # Lottery scheduling daemon
├── installer.sh      # Installation script
├── uninstaller.sh    # Uninstallation script
└── README.md         # This file
```

---

## Installation

### Automatic Installation (Recommended)

```bash
cd /home/bermuda/CS310/Project
sudo bash installer.sh
```

This script will:

- Compile `focusctl` and `focusd`
- Install binaries to `/usr/local/bin/`
- Create state directory `/var/lib/focusctl/`
- Set proper permissions

### Manual Installation

```bash
cd /home/bermuda/CS310/Project
gcc -o focusctl focusctl.c
gcc -o focusd focusd.c
sudo cp focusctl /usr/local/bin/
sudo cp focusd /usr/local/bin/
sudo mkdir -p /var/lib/focusctl
sudo chmod 755 /var/lib/focusctl
```

---

## Uninstallation

### Automatic Uninstallation

```bash
cd /home/bermuda/CS310/Project
sudo bash uninstaller.sh
```

This script will:

- Remove `/usr/local/bin/focusctl`
- Remove `/usr/local/bin/focusd`
- Remove state directory `/var/lib/focusctl/`
- Clean up cgroup configurations

### Manual Uninstallation

```bash
sudo rm /usr/local/bin/focusctl
sudo rm /usr/local/bin/focusd
sudo rm -rf /var/lib/focusctl
```

---

## Usage

### ⚠️ Important: Root Privileges Required

**All commands must be run with `sudo`**

```bash
sudo focusctl <command> [options]
sudo focusd <timeslice_ms>
```

### Initialize cgroups

```bash
sudo focusctl init
```

Sets up focus and background cgroups with CPU weights (focus=1000, background=10).

### Move process to focus group

```bash
sudo focusctl focus <pid>
```

**Example:**

```bash
sudo focusctl focus 1234
```

### Move process to background group

```bash
sudo focusctl background <pid>
```

**Example:**

```bash
sudo focusctl background 5678
```

### Unfocus a process

```bash
sudo focusctl unfocus <pid>
```

Returns process to root cgroup (neutral priority).

### Move processes by name

```bash
sudo focusctl focus-name <substring>
sudo focusctl background-name <substring>
```

**Examples:**

```bash
sudo focusctl focus-name firefox
sudo focusctl background-name chrome
```

Moves all processes containing the substring in their name.

### Pomodoro timer

```bash
sudo focusctl pomodoro <minutes> <pid1> [pid2 ...]
```

Boosts specified processes for a timed interval, then resets to equal priority.

**Example:**

```bash
sudo focusctl pomodoro 25 1234 5678
```

Boosts PIDs 1234 and 5678 for 25 minutes.

### View process groups

```bash
sudo focusctl status
```

Displays which processes are in focus and background groups.

### Stop all focused processes

```bash
sudo focusctl stop-all [--force]
```

Sends SIGTERM (or SIGKILL with `--force`) to all processes in focus group.

**Example:**

```bash
sudo focusctl stop-all           # SIGTERM
sudo focusctl stop-all --force   # SIGKILL
```

### Reset CPU weights

```bash
sudo focusctl relax
```

Sets both groups to equal priority (weight=100).

---

## focusd - Lottery Scheduler Daemon

**focusd** is a user-level lottery scheduler that:

- Reads processes and ticket allocations from `/var/lib/focusctl/procs.txt`
- Periodically selects a winner based on ticket proportion
- Moves the winner to focus group, others to background

### Start the daemon

```bash
sudo focusd <timeslice_ms>
```

Where `<timeslice_ms>` is the reschedule interval in milliseconds.

**Example:**

```bash
sudo focusd 100
```

Reschedules every 100 milliseconds.

**To run in background:**

```bash
sudo focusd 100 &
```

**To stop the daemon:**

```bash
sudo pkill focusd
```

### Add processes to lottery

```bash
sudo focusctl add <pid> <tickets>
```

**Example:**

```bash
sudo focusctl add 1234 50
sudo focusctl add 5678 30
```

Process 1234 has 50 tickets (62.5% chance of focus), process 5678 has 30 tickets (37.5% chance).

### Add processes by name

```bash
sudo focusctl add-name <substring> <tickets>
```

**Example:**

```bash
sudo focusctl add-name code 100
```

### List managed processes

```bash
sudo focusctl list
```

Displays PID and ticket count for all tracked processes.

### Remove process from lottery

```bash
sudo focusctl remove <pid>
```

**Example:**

```bash
sudo focusctl remove 1234
```

---

## Configuration

- **Cgroup paths**: `/sys/fs/cgroup/focus`, `/sys/fs/cgroup/background`
- **State file**: `/var/lib/focusctl/procs.txt` (process/ticket pairs)
- **Default focus weight**: 1000 (10x higher priority)
- **Default background weight**: 10

---

## Examples

### Example 1: Manual focus for studying

```bash
sudo focusctl init
sudo focusctl focus-name code        # Focus code editor
sudo focusctl background-name firefox # Minimize browser
sleep 3600                            # Study for 1 hour
sudo focusctl relax                   # Equal priority again
```

### Example 2: Lottery scheduling with daemon

```bash
# Setup
sudo focusctl init

# Add processes to lottery
sudo focusctl add 2000 100  # Main project: 100 tickets (67%)
sudo focusctl add 2100 50   # Secondary task: 50 tickets (33%)

# Start scheduler daemon
sudo focusd 200 &           # Check every 200ms

# View status
sudo focusctl status
sudo focusctl list

# Stop daemon
sudo pkill focusd
```

### Example 3: Pomodoro session

```bash
sudo focusctl init
sudo focusctl pomodoro 25 1234  # Focus PID 1234 for 25 minutes
# After 25 minutes, weights automatically reset to equal priority
```

### Example 4: Manage multiple processes by name

```bash
sudo focusctl init

# Focus all browser processes
sudo focusctl focus-name chromium

# Send background processes
sudo focusctl background-name slack

# View current state
sudo focusctl status
```

---

## Troubleshooting

### "cgroup v2 not found"

```
Error: cgroup v2 not found at /sys/fs/cgroup
```

**Solution:**

- Ensure kernel supports cgroups v2
- Check: `ls /sys/fs/cgroup/cgroup.controllers`
- May need to enable in kernel boot parameters

### "Permission denied" or "Operation not permitted"

```
Error: Permission denied
```

**Solution:**

- Ensure all commands are run with `sudo`
- Verify you have sudo privileges: `sudo whoami`

### "Failed to move pid"

**Possible causes:**

- Process has already exited
- Process belongs to different namespace
- Insufficient permissions

**Solution:**

- Verify process exists: `ps -p <pid>`
- Check process status: `cat /proc/<pid>/status`

### focusd not scheduling

**Solution:**

1. Ensure `/var/lib/focusctl/procs.txt` has valid entries
2. Check focusd is running: `ps aux | grep focusd`
3. View entries: `sudo focusctl list`
4. Check system logs: `sudo dmesg | tail`

### Cannot write to `/var/lib/focusctl/procs.txt`

**Solution:**

- Ensure directory exists: `sudo ls -la /var/lib/focusctl/`
- Check permissions: `sudo chmod 755 /var/lib/focusctl/`
- Reinstall: `sudo bash installer.sh`

---

## Limitations

- **Root privileges required** for all cgroup operations
- **Linux-only** with cgroups v2 support
- PIDs in `procs.txt` are not auto-cleaned when processes exit (use `remove` manually)
- **No persistence** across reboot (re-add processes after restart)
- Works best with CPU-bound processes (I/O wait may affect scheduling)

---

## Technical Details

### How cgroups v2 CPU Weight Works

- Processes in the same cgroup compete for CPU proportionally to their weight
- Higher weight = more CPU time
- Weight ratio determines actual CPU time ratio

**Example:**

- Focus group weight: 1000
- Background group weight: 10
- Ratio: 1000:10 = 100:1
- If both groups have equal load, focus gets ~99% CPU

### Lottery Scheduling Algorithm

1. Load all (pid, tickets) pairs from `procs.txt`
2. Calculate total tickets
3. Pick random number: 1 to total_tickets
4. Iterate through processes, accumulating tickets until threshold reached
5. Selected process moves to focus group
6. All others move to background group
7. Repeat every timeslice milliseconds

---

## Building from Source

### Prerequisites

```bash
sudo apt-get install build-essential  # For GCC and make
```

### Compile

```bash
cd /home/bermuda/CS310/Project
gcc -Wall -O2 -o focusctl focusctl.c
gcc -Wall -O2 -o focusd focusd.c
```

### Clean build artifacts

```bash
make clean  # if Makefile exists
# or
rm -f focusctl focusd
```

---

## Security Considerations

- **Root privileges required**: This tool needs root to access cgroups
- **Use only on trusted systems**
- **Be careful with `stop-all --force`**: Can abruptly kill processes
- **Monitor daemon resources**: focusd is lightweight but runs continuously

---

## Performance Tips

1. **Timeslice selection**:

   - Smaller (50ms): More responsive, higher overhead
   - Larger (500ms): Less responsive, lower overhead
   - Typical: 100-200ms

2. **Ticket allocation**:

   - Use proportional values (e.g., 100:50 instead of 2:1)
   - Avoid very large differences (>1000x) for fairness

3. **Process selection**:
   - Add only actively running processes
   - Remove finished processes with `remove`

---

## Author

CS310 Project - Process Scheduling with cgroups v2

## License

Educational use

---

## Support

For issues or questions:

1. Check the Troubleshooting section
2. Verify installation with `sudo focusctl init`
3. Check system logs: `sudo dmesg`
4. Ensure cgroups v2 support: `cat /proc/cmdline | grep cgroup`
