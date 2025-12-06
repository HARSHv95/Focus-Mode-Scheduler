#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>

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
                    fprintf(sc, "+cpu\n");
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
        return -1;
    }
    return 0;
}

static int load_ticket_entries(struct ticket_entry **out_arr, int *out_count)
{
    *out_arr = NULL;
    *out_count = 0;

    if (ensure_dir(STATE_DIR) < 0)
        return -1;

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

    int capacity = 16;
    int count = 0;
    struct ticket_entry *arr = (struct ticket_entry *)malloc(sizeof(struct ticket_entry) * capacity);
    if (!arr)
    {
        fclose(f);
        return -1;
    }

    while (1)
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
        if (pid_i <= 0)
            continue;

        if (count >= capacity)
        {
            capacity *= 2;
            struct ticket_entry *tmp =
                (struct ticket_entry *)realloc(arr, sizeof(struct ticket_entry) * capacity);
            if (!tmp)
            {
                free(arr);
                fclose(f);
                return -1;
            }
            arr = tmp;
        }

        arr[count].pid = (pid_t)pid_i;
        arr[count].tickets = tickets;
        count++;
    }

    fclose(f);

    *out_arr = arr;
    *out_count = count;
    return 0;
}

static pid_t pick_winner(struct ticket_entry *arr, int count)
{
    if (count <= 0)
        return -1;

    long total = 0;
    for (int i = 0; i < count; i++)
    {
        if (arr[i].tickets > 0)
            total += arr[i].tickets;
    }
    if (total <= 0)
        return -1;

    long r = (rand() % total) + 1; // 1..total

    long acc = 0;
    for (int i = 0; i < count; i++)
    {
        if (arr[i].tickets <= 0)
            continue;
        acc += arr[i].tickets;
        if (r <= acc)
        {
            return arr[i].pid;
        }
    }
    return arr[count - 1].pid;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr,
                "Usage: %s <timeslice_ms>\n"
                "Example: sudo %s 100\n",
                argv[0], argv[0]);
        return 1;
    }

    int timeslice_ms = atoi(argv[1]);
    if (timeslice_ms <= 0)
    {
        fprintf(stderr, "timeslice_ms must be > 0\n");
        return 1;
    }

    if (init_cgroups() < 0)
    {
        fprintf(stderr, "Failed to init cgroups.\n");
        return 1;
    }

    srand((unsigned int)time(NULL));

    printf("focusd: user-level lottery scheduler started (timeslice=%d ms).\n", timeslice_ms);
    printf("It will read %s for (pid, tickets) entries.\n", PROCS_FILE);

    for (;;)
    {
        struct ticket_entry *arr = NULL;
        int count = 0;

        if (load_ticket_entries(&arr, &count) < 0)
        {
            fprintf(stderr, "Error loading ticket entries. Sleeping...\n");
            usleep(timeslice_ms * 1000);
            continue;
        }

        if (count <= 0)
        {
            // nothing to schedule
            usleep(timeslice_ms * 1000);
            free(arr);
            continue;
        }

        pid_t winner = pick_winner(arr, count);

        if (winner > 0)
        {
            for (int i = 0; i < count; i++)
            {
                if (arr[i].pid == winner)
                {
                    move_pid(FOCUS_NAME, arr[i].pid);
                }
                else
                {
                    move_pid(BG_NAME, arr[i].pid);
                }
            }
        }

        free(arr);
        usleep(timeslice_ms * 1000);
    }

    return 0;
}