// focusctl.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> // pid_t
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <ctype.h>
#include <signal.h> // kill, SIGTERM, SIGKILL

#define CGROUP_ROOT "/sys/fs/cgroup"
#define FOCUS_NAME "focus"
#define BG_NAME "background"

#define STATE_DIR "/var/lib/focusctl"
#define PROCS_FILE STATE_DIR "/procs.txt"

struct ticket_entry
{
    pid_t pid;
    int tickets;
};

static int write_file(const char *path, const char *value)
{
    FILE *f = fopen(path, "w");
    if (!f)
    {
        perror(path);
        return -1;
    }
    if (fprintf(f, "%s\n", value) < 0)
    {
        perror("fprintf");
        fclose(f);
        return -1;
    }
    if (fclose(f) != 0)
    {
        perror("fclose");
        return -1;
    }
    return 0;
}

static int ensure_dir(const char *path)
{
    if (mkdir(path, 0755) < 0)
    {
        if (errno == EEXIST)
            return 0;
        perror(path);
        return -1;
    }
    return 0;
}

static int check_cgroup_v2(void)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/cgroup.controllers", CGROUP_ROOT);
    if (access(path, F_OK) != 0)
    {
        fprintf(stderr, "cgroup v2 not found at %s\n", CGROUP_ROOT);
        return -1;
    }
    return 0;
}

static int init_cgroups(void)
{
    char path[256];

    if (check_cgroup_v2() < 0)
    {
        return -1;
    }

    snprintf(path, sizeof(path), "%s/cgroup.subtree_control", CGROUP_ROOT);
    FILE *sc = fopen(path, "r+");
    if (sc)
    {
        char buf[256] = {0};
        if (fgets(buf, sizeof(buf), sc))
        {
            if (strstr(buf, "cpu") == NULL)
            {
                fclose(sc);
                sc = fopen(path, "w");
                if (sc)
                {
                    if (fprintf(sc, "+cpu\n") < 0)
                    {
                        perror("write subtree_control");
                    }
                    fclose(sc);
                }
            }
            else
            {
                fclose(sc);
            }
        }
        else
        {
            fclose(sc);
        }
    }

    snprintf(path, sizeof(path), "%s/%s", CGROUP_ROOT, FOCUS_NAME);
    if (ensure_dir(path) < 0)
        return -1;

    snprintf(path, sizeof(path), "%s/%s", CGROUP_ROOT, BG_NAME);
    if (ensure_dir(path) < 0)
        return -1;

    char cpu_path[256];

    snprintf(cpu_path, sizeof(cpu_path), "%s/%s/cpu.weight", CGROUP_ROOT, FOCUS_NAME);
    if (write_file(cpu_path, "1000") < 0)
        return -1;

    snprintf(cpu_path, sizeof(cpu_path), "%s/%s/cpu.weight", CGROUP_ROOT, BG_NAME);
    if (write_file(cpu_path, "10") < 0)
        return -1;

    if (ensure_dir(STATE_DIR) < 0)
        return -1;

    printf("Initialized focus and background cgroups (focus=1000, background=10).\n");
    return 0;
}

static int move_pid(const char *group, pid_t pid)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s/cgroup.procs", CGROUP_ROOT, group);

    char buf[32];
    snprintf(buf, sizeof(buf), "%d", pid);

    if (write_file(path, buf) < 0)
    {
        fprintf(stderr, "Failed to move pid %d to %s\n", pid, group);
        return -1;
    }

    printf("Moved pid %d to %s group.\n", pid, group);
    return 0;
}

static int move_pid_root(pid_t pid)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/cgroup.procs", CGROUP_ROOT);

    char buf[32];
    snprintf(buf, sizeof(buf), "%d", pid);

    if (write_file(path, buf) < 0)
    {
        fprintf(stderr, "Failed to move pid %d to root cgroup\n", pid);
        return -1;
    }

    printf("Moved pid %d back to root cgroup (unfocused).\n", pid);
    return 0;
}

static int reset_weights(void)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s/cpu.weight", CGROUP_ROOT, FOCUS_NAME);
    if (write_file(path, "100") < 0)
        return -1;

    snprintf(path, sizeof(path), "%s/%s/cpu.weight", CGROUP_ROOT, BG_NAME);
    if (write_file(path, "100") < 0)
        return -1;

    printf("Reset cpu.weight of focus and background to 100.\n");
    return 0;
}

static int print_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
    {
        perror(path);
        return -1;
    }
    int c;
    while ((c = fgetc(f)) != EOF)
    {
        putchar(c);
    }
    fclose(f);
    return 0;
}

static int status_cmd(void)
{
    char path[256];

    printf("=== Focus group ===\n");
    snprintf(path, sizeof(path), "%s/%s/cgroup.procs", CGROUP_ROOT, FOCUS_NAME);
    print_file(path);

    printf("\n=== Background group ===\n");
    snprintf(path, sizeof(path), "%s/%s/cgroup.procs", CGROUP_ROOT, BG_NAME);
    print_file(path);
    printf("\n");

    return 0;
}

static int is_number_str(const char *s)
{
    if (!s || !*s)
        return 0;
    for (const char *p = s; *p; p++)
    {
        if (!isdigit((unsigned char)*p))
            return 0;
    }
    return 1;
}

static int move_by_name(const char *group, const char *name)
{
    DIR *d = opendir("/proc");
    if (!d)
    {
        perror("opendir /proc");
        return -1;
    }

    struct dirent *ent;
    int moved = 0;

    while ((ent = readdir(d)) != NULL)
    {
        if (!is_number_str(ent->d_name))
            continue;

        pid_t pid = (pid_t)atoi(ent->d_name);
        char comm_path[256];
        snprintf(comm_path, sizeof(comm_path), "/proc/%d/comm", pid);

        FILE *f = fopen(comm_path, "r");
        if (!f)
            continue;

        char comm[256] = {0};
        if (fgets(comm, sizeof(comm), f) == NULL)
        {
            fclose(f);
            continue;
        }
        fclose(f);

        size_t len = strlen(comm);
        if (len > 0 && comm[len - 1] == '\n')
            comm[len - 1] = '\0';

        if (strstr(comm, name) != NULL)
        {
            move_pid(group, pid);
            moved++;
        }
    }

    closedir(d);

    if (moved == 0)
    {
        printf("No processes found with name containing \"%s\".\n", name);
    }
    else
    {
        printf("Moved %d processes matching \"%s\" to %s group.\n",
               moved, name, group);
    }
    return 0;
}

static int pomodoro_cmd(int minutes, int npids, char **pid_args)
{
    if (minutes <= 0)
    {
        fprintf(stderr, "Minutes must be > 0\n");
        return -1;
    }
    if (npids <= 0)
    {
        fprintf(stderr, "At least one PID is required for pomodoro.\n");
        return -1;
    }

    if (init_cgroups() < 0)
    {
        fprintf(stderr, "Failed to init cgroups for pomodoro.\n");
        return -1;
    }

    for (int i = 0; i < npids; i++)
    {
        if (!is_number_str(pid_args[i]))
        {
            fprintf(stderr, "Invalid pid: %s\n", pid_args[i]);
            continue;
        }
        pid_t pid = (pid_t)atoi(pid_args[i]);
        move_pid(FOCUS_NAME, pid);
    }

    int total_seconds = minutes * 60;
    printf("Pomodoro started for %d minute(s). Focus group boosted.\n", minutes);
    printf("Sleeping for %d seconds...\n", total_seconds);
    sleep(total_seconds);

    printf("Pomodoro finished. Resetting weights.\n");
    reset_weights();

    return 0;
}

static int stop_all_focus(int force)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s/cgroup.procs", CGROUP_ROOT, FOCUS_NAME);

    FILE *f = fopen(path, "r");
    if (!f)
    {
        perror(path);
        return -1;
    }

    int killed = 0;
    char line[64];
    int sig = force ? SIGKILL : SIGTERM;

    while (fgets(line, sizeof(line), f))
    {
        char *nl = strchr(line, '\n');
        if (nl)
            *nl = '\0';
        if (!is_number_str(line))
            continue;
        pid_t pid = (pid_t)atoi(line);
        if (pid <= 0)
            continue;

        if (kill(pid, sig) == 0)
        {
            killed++;
        }
        else
        {
            perror("kill");
        }
    }
    fclose(f);

    if (killed == 0)
    {
        printf("No processes to stop in focus group.\n");
    }
    else
    {
        printf("Sent %s to %d process(es) in focus group.\n",
               force ? "SIGKILL" : "SIGTERM", killed);
    }
    return 0;
}

static int load_ticket_entries(struct ticket_entry *arr, int max_entries, int *out_count)
{
    *out_count = 0;

    FILE *f = fopen(PROCS_FILE, "r");
    if (!f)
    {
        if (errno == ENOENT)
        {
            return 0;
        }
        perror(PROCS_FILE);
        return -1;
    }

    int count = 0;
    while (count < max_entries)
    {
        int pid_i = 0;
        int tickets = 0;
        int n = fscanf(f, "%d %d", &pid_i, &tickets);
        if (n == EOF)
            break;
        if (n != 2)
        {
            char buf[256];
            if (!fgets(buf, sizeof(buf), f))
                break;
            continue;
        }
        if (tickets <= 0)
            continue;

        arr[count].pid = (pid_t)pid_i;
        arr[count].tickets = tickets;
        count++;
    }

    fclose(f);
    *out_count = count;
    return 0;
}

static int save_ticket_entries(struct ticket_entry *arr, int count)
{
    if (ensure_dir(STATE_DIR) < 0)
        return -1;

    FILE *f = fopen(PROCS_FILE, "w");
    if (!f)
    {
        perror(PROCS_FILE);
        return -1;
    }

    for (int i = 0; i < count; i++)
    {
        if (arr[i].tickets <= 0 || arr[i].pid <= 0)
            continue;
        fprintf(f, "%d %d\n", arr[i].pid, arr[i].tickets);
    }

    if (fclose(f) != 0)
    {
        perror("fclose");
        return -1;
    }
    return 0;
}

static int cmd_add(pid_t pid, int tickets)
{
    if (tickets <= 0)
    {
        fprintf(stderr, "Tickets must be > 0\n");
        return -1;
    }

    struct ticket_entry entries[1024];
    int count = 0;
    if (load_ticket_entries(entries, 1024, &count) < 0)
        return -1;

    int found = 0;
    for (int i = 0; i < count; i++)
    {
        if (entries[i].pid == pid)
        {
            entries[i].tickets = tickets;
            found = 1;
            break;
        }
    }
    if (!found)
    {
        if (count >= 1024)
        {
            fprintf(stderr, "Too many managed processes.\n");
            return -1;
        }
        entries[count].pid = pid;
        entries[count].tickets = tickets;
        count++;
    }

    if (save_ticket_entries(entries, count) < 0)
        return -1;

    if (found)
        printf("Updated pid %d tickets to %d.\n", pid, tickets);
    else
        printf("Added pid %d with %d tickets.\n", pid, tickets);

    return 0;
}

static int add_by_name(const char *name, int tickets)
{
    if (tickets <= 0)
    {
        fprintf(stderr, "Tickets must be > 0\n");
        return -1;
    }

    DIR *d = opendir("/proc");
    if (!d)
    {
        perror("opendir /proc");
        return -1;
    }

    struct dirent *ent;
    int added = 0;

    while ((ent = readdir(d)) != NULL)
    {
        if (!is_number_str(ent->d_name))
            continue;

        pid_t pid = (pid_t)atoi(ent->d_name);
        char comm_path[256];
        snprintf(comm_path, sizeof(comm_path), "/proc/%d/comm", pid);

        FILE *f = fopen(comm_path, "r");
        if (!f)
            continue;

        char comm[256] = {0};
        if (!fgets(comm, sizeof(comm), f))
        {
            fclose(f);
            continue;
        }
        fclose(f);

        size_t len = strlen(comm);
        if (len > 0 && comm[len - 1] == '\n')
            comm[len - 1] = '\0';

        if (strstr(comm, name) != NULL)
        {
            // reuse existing cmd_add(pid, tickets)
            if (cmd_add(pid, tickets) == 0)
            {
                added++;
            }
        }
    }

    closedir(d);

    if (added == 0)
    {
        printf("No processes found with name containing \"%s\".\n", name);
    }
    else
    {
        printf("Added/updated %d processes matching \"%s\" with %d tickets.\n",
               added, name, tickets);
    }
    return 0;
}

static int cmd_remove(pid_t pid)
{
    struct ticket_entry entries[1024];
    int count = 0;
    if (load_ticket_entries(entries, 1024, &count) < 0)
        return -1;

    int new_count = 0;
    for (int i = 0; i < count; i++)
    {
        if (entries[i].pid == pid)
            continue;
        entries[new_count++] = entries[i];
    }

    if (save_ticket_entries(entries, new_count) < 0)
        return -1;

    printf("Removed pid %d from lottery list (if it was present).\n", pid);
    return 0;
}

static int cmd_list(void)
{
    struct ticket_entry entries[1024];
    int count = 0;
    if (load_ticket_entries(entries, 1024, &count) < 0)
        return -1;

    if (count == 0)
    {
        printf("No processes registered for lottery scheduling.\n");
        return 0;
    }

    printf("PID\tTickets\n");
    printf("----\t-------\n");
    for (int i = 0; i < count; i++)
    {
        printf("%d\t%d\n", entries[i].pid, entries[i].tickets);
    }
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr,
                "Usage:\n"
                "  %s init\n"
                "  %s focus <pid>\n"
                "  %s background <pid>\n"
                "  %s unfocus <pid>\n"
                "  %s focus-name <substring>\n"
                "  %s background-name <substring>\n"
                "  %s pomodoro <minutes> <pid1> [pid2 ...]\n"
                "  %s stop-all [--force]\n"
                "  %s relax\n"
                "  %s status\n"
                "  %s add <pid> <tickets>\n"
                "  %s remove <pid>\n"
                "  %s list\n"
                "  %s add-name <substring> <tickets>\n",
                argv[0], argv[0], argv[0], argv[0], argv[0],
                argv[0], argv[0], argv[0], argv[0], argv[0],
                argv[0], argv[0], argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "init") == 0)
    {
        return init_cgroups();
    }
    else if (strcmp(argv[1], "focus") == 0)
    {
        if (argc < 3)
        {
            fprintf(stderr, "Usage: %s focus <pid>\n", argv[0]);
            return 1;
        }
        pid_t pid = (pid_t)atoi(argv[2]);
        return move_pid(FOCUS_NAME, pid);
    }
    else if (strcmp(argv[1], "background") == 0)
    {
        if (argc < 3)
        {
            fprintf(stderr, "Usage: %s background <pid>\n", argv[0]);
            return 1;
        }
        pid_t pid = (pid_t)atoi(argv[2]);
        return move_pid(BG_NAME, pid);
    }
    else if (strcmp(argv[1], "unfocus") == 0)
    {
        if (argc < 3)
        {
            fprintf(stderr, "Usage: %s unfocus <pid>\n", argv[0]);
            return 1;
        }
        pid_t pid = (pid_t)atoi(argv[2]);
        return move_pid_root(pid);
    }
    else if (strcmp(argv[1], "focus-name") == 0)
    {
        if (argc < 3)
        {
            fprintf(stderr, "Usage: %s focus-name <substring>\n", argv[0]);
            return 1;
        }
        return move_by_name(FOCUS_NAME, argv[2]);
    }
    else if (strcmp(argv[1], "background-name") == 0)
    {
        if (argc < 3)
        {
            fprintf(stderr, "Usage: %s background-name <substring>\n", argv[0]);
            return 1;
        }
        return move_by_name(BG_NAME, argv[2]);
    }
    else if (strcmp(argv[1], "pomodoro") == 0)
    {
        if (argc < 4)
        {
            fprintf(stderr, "Usage: %s pomodoro <minutes> <pid1> [pid2 ...]\n", argv[0]);
            return 1;
        }
        int minutes = atoi(argv[2]);
        return pomodoro_cmd(minutes, argc - 3, &argv[3]);
    }
    else if (strcmp(argv[1], "stop-all") == 0)
    {
        int force = 0;
        if (argc >= 3 && strcmp(argv[2], "--force") == 0)
        {
            force = 1;
        }
        return stop_all_focus(force);
    }
    else if (strcmp(argv[1], "relax") == 0)
    {
        return reset_weights();
    }
    else if (strcmp(argv[1], "status") == 0)
    {
        return status_cmd();
    }
    else if (strcmp(argv[1], "add") == 0)
    {
        if (argc < 4)
        {
            fprintf(stderr, "Usage: %s add <pid> <tickets>\n", argv[0]);
            return 1;
        }
        pid_t pid = (pid_t)atoi(argv[2]);
        int tickets = atoi(argv[3]);
        return cmd_add(pid, tickets);
    }
    else if (strcmp(argv[1], "add-name") == 0)
    {
        if (argc < 4)
        {
            fprintf(stderr, "Usage: %s add-name <substring> <tickets>\n", argv[0]);
            return 1;
        }
        int tickets = atoi(argv[3]);
        return add_by_name(argv[2], tickets);
    }
    else if (strcmp(argv[1], "remove") == 0)
    {
        if (argc < 3)
        {
            fprintf(stderr, "Usage: %s remove <pid>\n", argv[0]);
            return 1;
        }
        pid_t pid = (pid_t)atoi(argv[2]);
        return cmd_remove(pid);
    }
    else if (strcmp(argv[1], "list") == 0)
    {
        return cmd_list();
    }
    else
    {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
        return 1;
    }
}
