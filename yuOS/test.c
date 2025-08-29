#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>

#define MAX 1024
#define PATH "/proc"

struct child {
    int index;
    struct child *next;
};

struct proc {
    char name[256];
    __pid_t ppid;
    __pid_t pid;
    struct child *head;
};

int proc_num = 0;
struct proc array[MAX];
bool show_pids = false;

void read_proc(const char *proc_pid) {
    char file_path[256];
    snprintf(file_path, sizeof(file_path), "%s/%s/stat", PATH, proc_pid);
    
    FILE *file = fopen(file_path, "r");
    if (!file) return;

    int pid, ppid;
    char name[256];
    char state;
    
    if (fscanf(file, "%d (%[^)]) %c %d", &pid, name, &state, &ppid) == 4) {
        if (proc_num < MAX) {
            array[proc_num].pid = pid;
            snprintf(array[proc_num].name, sizeof(array[proc_num].name), "%s", name);
            array[proc_num].ppid = ppid;
            array[proc_num].head = NULL;
            proc_num++;
        }
    }
    fclose(file);
}

void scan_proc() {
    DIR *dir = opendir(PATH);
    if (!dir) {
        perror("Failed to open /proc directory");
        exit(EXIT_FAILURE);
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (isdigit(entry->d_name[0])) {
            read_proc(entry->d_name);
        }
    }
    closedir(dir);
}

int p_index(__pid_t pid) {
    for (int i = 0; i < proc_num; i++) {
        if (array[i].pid == pid) {
            return i;
        }
    }
    return -1;
}

void build_list(int pindex, int cindex) {
    struct child *new_child = malloc(sizeof(struct child));
    new_child->index = cindex;
    new_child->next = NULL;

    if (array[pindex].head == NULL) {
        array[pindex].head = new_child;
        return;
    }

    // Insert in sorted order
    if (array[cindex].pid < array[array[pindex].head->index].pid) {
        new_child->next = array[pindex].head;
        array[pindex].head = new_child;
        return;
    }

    struct child *current = array[pindex].head;
    while (current->next != NULL && 
           array[cindex].pid > array[current->next->index].pid) {
        current = current->next;
    }
    new_child->next = current->next;
    current->next = new_child;
}

void build_tree() {
    int init_index = p_index(1); // Find init process (pid=1)
    
    for (int i = 0; i < proc_num; i++) {
        if (array[i].pid == 1) continue; // Skip init itself
        
        int pindex = p_index(array[i].ppid);
        if (pindex == -1) {
            // Orphan process, attach to init
            if (init_index != -1) {
                build_list(init_index, i);
            }
        } else {
            build_list(pindex, i);
        }
    }
}

void print_tree(int index, int depth) {
    // Print indentation
    for (int i = 0; i < depth; i++) {
        printf("    ");
    }
    
    // Print process name
    printf("%s", array[index].name);
    
    // Print pid if requested
    if (show_pids) {
        printf("(%d)", array[index].pid);
    }
    printf("\n");
    
    // Recursively print children
    struct child *current = array[index].head;
    while (current != NULL) {
        print_tree(current->index, depth + 1);
        current = current->next;
    }
    return;
}

void free_tree() {
    for (int i = 0; i < proc_num; i++) {
        struct child *current = array[i].head;
        while (current != NULL) {
            struct child *next = current->next;
            free(current);
            current = next;
        }
    }
}

int main(int argc, char *argv[]) {
    static struct option options[] = {
        {"show-pids", no_argument, NULL, 'p'},
        {"numeric-sort", no_argument, NULL, 'n'},
        {"version", no_argument, NULL, 'V'},
        {NULL, 0, NULL, 0},
    };
    int opt;
    while ((opt = getopt_long(argc, argv, "npV", options, NULL)) != -1) {
        switch (opt) {
        case 'p':
            show_pids = true;
            break;
        case 'V':
            printf("pstree\n");
            return 0;
        case 'n':
            break;
        default:
            fprintf(stderr, "Usage: %s [-p]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    scan_proc();
    build_tree();
    
    // Find and print from init process (pid=1)
    int init_index = p_index(1);
    if (init_index != -1) {
        print_tree(init_index, 0);
    }
    
    free_tree();
    return 0;
}