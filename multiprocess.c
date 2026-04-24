#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <unistd.h>

#define MAX_WORD_LENGTH 70
#define MAX_UNIQUE_WORDS_PER_CHILD 10000

typedef struct {
    char word[MAX_WORD_LENGTH];
    int frequency;
} WordArray;

// Convert a string to lowercase in place
void toLowerCase(char *str) {
    for (int i = 0; str[i]; i++) {
        str[i] = tolower((unsigned char)str[i]);
    }
}

// Strip leading and trailing non-alphanumeric characters
void stripPunctuation(char *str) {
    if (str[0] == '\0') return;

    int start = 0;
    int end = strlen(str) - 1;

    while (start <= end && !isalnum((unsigned char)str[start])) {
        start++;
    }
    while (end >= start && !isalnum((unsigned char)str[end])) {
        end--;
    }

    int length = end - start + 1;
    if (start > 0) {
        memmove(str, str + start, length);
    }
    str[length] = '\0';
}

// Merge two sorted halves (descending by frequency)
void merge(WordArray *arr, int left, int mid, int right) {
    int n1 = mid - left + 1;
    int n2 = right - mid;

    WordArray *L = malloc(n1 * sizeof(WordArray));
    WordArray *R = malloc(n2 * sizeof(WordArray));

    if (!L || !R) {
        perror("Error allocating memory in merge");
        free(L);
        free(R);
        exit(1);
    }

    for (int i = 0; i < n1; i++) {
        L[i] = arr[left + i];
    }
    for (int j = 0; j < n2; j++) {
        R[j] = arr[mid + 1 + j];
    }

    int i = 0, j = 0, k = left;
    while (i < n1 && j < n2) {
        if (L[i].frequency >= R[j].frequency) {
            arr[k] = L[i];
            i++;
        } else {
            arr[k] = R[j];
            j++;
        }
        k++;
    }

    while (i < n1) {
        arr[k] = L[i];
        i++;
        k++;
    }
    while (j < n2) {
        arr[k] = R[j];
        j++;
        k++;
    }

    free(L);
    free(R);
}

void mergeSort(WordArray *arr, int left, int right) {
    if (left < right) {
        int mid = left + (right - left) / 2;
        mergeSort(arr, left, mid);
        mergeSort(arr, mid + 1, right);
        merge(arr, left, mid, right);
    }
}

// Print top 10 most frequent words
void printTop10(WordArray *arr, int size) {
    printf("Top 10 Most Frequent Words:\n");
    int printed = 0;
    for (int i = 0; i < size && printed < 10; i++) {
        if (arr[i].frequency > 0) {
            printf("%s: %d\n", arr[i].word, arr[i].frequency);
            printed++;
        }
    }
}

// Shared memory layout per child:
//   - First sizeof(int) bytes: the count of words this child found
//   - Then MAX_UNIQUE_WORDS_PER_CHILD WordArray entries
typedef struct {
    int count;
    WordArray words[MAX_UNIQUE_WORDS_PER_CHILD];
} ChildSlot;

// Count word frequencies in a text chunk.
// Writes results directly into the given ChildSlot.
void countFrequencyChunk(ChildSlot *slot, const char *chunk, long chunkSize) {
    char buffer[MAX_WORD_LENGTH];
    int wordCount = 0;

    // Make a mutable copy of the chunk so strtok can modify it
    char *chunkCopy = strndup(chunk, chunkSize);
    if (!chunkCopy) {
        perror("strndup failed");
        exit(1);
    }

    char *token = strtok(chunkCopy, " \t\n\r");
    while (token != NULL) {
        // Copy token into buffer, normalize
        strncpy(buffer, token, MAX_WORD_LENGTH - 1);
        buffer[MAX_WORD_LENGTH - 1] = '\0';
        toLowerCase(buffer);
        stripPunctuation(buffer);

        if (buffer[0] == '\0') {
            token = strtok(NULL, " \t\n\r");
            continue;
        }

        // Search for existing entry
        int found = 0;
        for (int i = 0; i < wordCount; i++) {
            if (strcmp(slot->words[i].word, buffer) == 0) {
                slot->words[i].frequency++;
                found = 1;
                break;
            }
        }

        if (!found) {
            if (wordCount >= MAX_UNIQUE_WORDS_PER_CHILD) {
                fprintf(stderr, "Warning: child hit %d unique word limit, some words will be dropped\n",
                        MAX_UNIQUE_WORDS_PER_CHILD);
            } else {
                strncpy(slot->words[wordCount].word, buffer, MAX_WORD_LENGTH - 1);
                slot->words[wordCount].word[MAX_WORD_LENGTH - 1] = '\0';
                slot->words[wordCount].frequency = 1;
                wordCount++;
            }
        }

        token = strtok(NULL, " \t\n\r");
    }

    slot->count = wordCount;
    free(chunkCopy);
}

// Adjust a split point so it falls on a whitespace boundary.
// This prevents cutting a word in half between two children.
long adjustBoundary(const char *content, long totalSize, long pos) {
    while (pos < totalSize && !isspace((unsigned char)content[pos])) {
        pos++;
    }
    return pos;
}

// Fork child processes, each processing a chunk of the file
void divideWork(ChildSlot *slots, const char *fileContent, long totalSize, int numChildren) {
    long chunkSize = totalSize / numChildren;

    for (int i = 0; i < numChildren; i++) {
        long start = (i == 0) ? 0 : adjustBoundary(fileContent, totalSize, i * chunkSize);
        long end;
        if (i == numChildren - 1) {
            end = totalSize;
        } else {
            end = adjustBoundary(fileContent, totalSize, (i + 1) * chunkSize);
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork failed");
            exit(1);
        }

        if (pid == 0) {
            // Child process: count words in assigned chunk, write to own slot
            countFrequencyChunk(&slots[i], fileContent + start, end - start);
            exit(0);
        }
    }

    // Parent waits for all children
    for (int i = 0; i < numChildren; i++) {
        wait(NULL);
    }
}

// Merge results from all children, combining duplicate words
int mergeChildResults(ChildSlot *slots, int numChildren, WordArray **outResults) {
    // First pass: count total entries across all children for allocation
    int totalEntries = 0;
    for (int i = 0; i < numChildren; i++) {
        totalEntries += slots[i].count;
    }

    if (totalEntries == 0) {
        *outResults = NULL;
        return 0;
    }

    WordArray *merged = malloc(totalEntries * sizeof(WordArray));
    if (!merged) {
        perror("malloc for merged results");
        exit(1);
    }
    int mergedCount = 0;

    // Combine: for each word from every child, either add to existing or create new
    for (int c = 0; c < numChildren; c++) {
        for (int w = 0; w < slots[c].count; w++) {
            int found = 0;
            for (int m = 0; m < mergedCount; m++) {
                if (strcmp(merged[m].word, slots[c].words[w].word) == 0) {
                    merged[m].frequency += slots[c].words[w].frequency;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                merged[mergedCount] = slots[c].words[w];
                mergedCount++;
            }
        }
    }

    *outResults = merged;
    return mergedCount;
}

int main(int argc, char *argv[]) {
    struct timeval start, end;
    double duration;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <file_name> <num_children>\n", argv[0]);
        return 1;
    }

    const char *fileName = argv[1];
    int numChildren = atoi(argv[2]);

    if (numChildren <= 0) {
        fprintf(stderr, "Number of child processes must be a positive integer.\n");
        return 1;
    }

    if (numChildren > 128) {
        fprintf(stderr, "Too many child processes (max 128). Got %d.\n", numChildren);
        return 1;
    }

    gettimeofday(&start, NULL);

    FILE *file = fopen(fileName, "r");
    if (!file) {
        perror("Error opening file");
        return 1;
    }

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    rewind(file);

    char *fileContent = malloc(fileSize + 1);
    if (!fileContent) {
        perror("malloc for file content");
        fclose(file);
        return 1;
    }

    size_t bytesRead = fread(fileContent, 1, fileSize, file);
    fclose(file);
    if ((long)bytesRead != fileSize) {
        fprintf(stderr, "Warning: expected to read %ld bytes but got %zu\n", fileSize, bytesRead);
    }
    fileContent[bytesRead] = '\0';

    // Allocate shared memory for child slots
    size_t shmSize = numChildren * sizeof(ChildSlot);
    ChildSlot *slots = mmap(NULL, shmSize, PROT_READ | PROT_WRITE,
                            MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (slots == MAP_FAILED) {
        perror("mmap");
        free(fileContent);
        return 1;
    }
    memset(slots, 0, shmSize);

    // Fork children and process chunks
    divideWork(slots, fileContent, bytesRead, numChildren);

    // Merge and deduplicate results from all children
    WordArray *results = NULL;
    int resultCount = mergeChildResults(slots, numChildren, &results);

    if (resultCount > 0) {
        mergeSort(results, 0, resultCount - 1);
        printTop10(results, resultCount);
    } else {
        printf("No words found in the file.\n");
    }

    // Cleanup
    munmap(slots, shmSize);
    free(fileContent);
    free(results);

    gettimeofday(&end, NULL);
    duration = (end.tv_sec - start.tv_sec) * 1000000.0;
    duration += (end.tv_usec - start.tv_usec);
    printf("\nExecution time: %.6f seconds\n", duration / 1000000.0);

    return 0;
}
