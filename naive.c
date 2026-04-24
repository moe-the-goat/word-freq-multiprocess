#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define MAX_WORD_LENGTH 70

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

// Strip leading and trailing non-alphanumeric characters from a word.
// Modifies the string in place by shifting characters and null-terminating.
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

// Merge two sorted halves of the array (descending by frequency)
void merge(WordArray *words, int left, int mid, int right) {
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
        L[i] = words[left + i];
    }
    for (int j = 0; j < n2; j++) {
        R[j] = words[mid + 1 + j];
    }

    int i = 0, j = 0, k = left;
    while (i < n1 && j < n2) {
        if (L[i].frequency >= R[j].frequency) {
            words[k] = L[i];
            i++;
        } else {
            words[k] = R[j];
            j++;
        }
        k++;
    }

    while (i < n1) {
        words[k] = L[i];
        i++;
        k++;
    }

    while (j < n2) {
        words[k] = R[j];
        j++;
        k++;
    }

    free(L);
    free(R);
}

// Merge sort implementation (sorts descending by frequency)
void mergeSort(WordArray *words, int left, int right) {
    if (left < right) {
        int mid = left + (right - left) / 2;

        mergeSort(words, left, mid);
        mergeSort(words, mid + 1, right);
        merge(words, left, mid, right);
    }
}

// Read the given file, count unique word frequencies, sort, and print top 10
void countFrequency(const char *fileName) {
    WordArray *words = NULL;
    int wordCount = 0;
    int capacity = 10;
    char buffer[MAX_WORD_LENGTH];

    words = malloc(capacity * sizeof(WordArray));
    if (!words) {
        perror("Error allocating memory");
        exit(1);
    }

    FILE *file = fopen(fileName, "r");
    if (!file) {
        perror("Error opening file");
        free(words);
        exit(1);
    }

    // Read words from the file and count their frequencies
    while (fscanf(file, "%69s", buffer) != EOF) {
        // Normalize: lowercase and strip punctuation
        toLowerCase(buffer);
        stripPunctuation(buffer);

        // Skip tokens that became empty after stripping
        if (buffer[0] == '\0') {
            continue;
        }

        int found = 0;
        for (int i = 0; i < wordCount; i++) {
            if (strcmp(words[i].word, buffer) == 0) {
                words[i].frequency++;
                found = 1;
                break;
            }
        }

        if (!found) {
            if (wordCount == capacity) {
                capacity *= 2;
                WordArray *temp = realloc(words, capacity * sizeof(WordArray));
                if (!temp) {
                    perror("Error reallocating memory");
                    free(words);
                    fclose(file);
                    exit(1);
                }
                words = temp;
            }

            strncpy(words[wordCount].word, buffer, MAX_WORD_LENGTH - 1);
            words[wordCount].word[MAX_WORD_LENGTH - 1] = '\0';
            words[wordCount].frequency = 1;
            wordCount++;
        }
    }

    fclose(file);

    if (wordCount == 0) {
        printf("No words found in the file.\n");
        free(words);
        return;
    }

    // Sort by frequency (descending) and print top 10
    mergeSort(words, 0, wordCount - 1);
    printf("Top 10 Most Frequent Words:\n");
    for (int i = 0; i < 10 && i < wordCount; i++) {
        printf("%s: %d\n", words[i].word, words[i].frequency);
    }

    free(words);
}

int main(int argc, char *argv[]) {
    struct timeval start, end;
    double duration;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file_name>\n", argv[0]);
        return 1;
    }

    gettimeofday(&start, NULL);

    countFrequency(argv[1]);

    gettimeofday(&end, NULL);
    duration = (end.tv_sec - start.tv_sec) * 1000000.0;
    duration += (end.tv_usec - start.tv_usec);
    printf("\nExecution time: %.6f seconds\n", duration / 1000000.0);

    return 0;
}
