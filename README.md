# Word Frequency Counter: Single-Process vs Multiprocess

A side-by-side comparison of two approaches to counting word frequencies in C. The single-process version reads and counts everything sequentially. The multiprocess version splits the work across child processes using `fork()` and POSIX shared memory, then merges the results.

The assignment asked us to implement both approaches and compare their performance on large text files.

Both programs read a plain text file, normalize each word (lowercase, strip punctuation), count occurrences, and print the 10 most frequent words.

## Table of Contents

- [How It Works](#how-it-works)
- [Building](#building)
- [Usage](#usage)
- [Example Output](#example-output)
- [Project Structure](#project-structure)
- [Implementation Notes](#implementation-notes)
- [Performance](#performance)
- [Known Limitations](#known-limitations)

## How It Works

### Single-Process (naive.c)

Reads the file word by word with `fscanf`. Each word is normalized and looked up in a dynamically growing array. If it already exists, its count goes up. If not, it gets added. After the whole file is processed, the array is sorted by frequency (descending) using merge sort, and the top 10 are printed.

The lookup is a linear scan, which makes this O(n * m) where n is total words and m is unique words. Simple and correct, but not fast on large inputs.

### Multiprocess (multiprocess.c)

1. The parent reads the entire file into memory.
2. The file content is divided into roughly equal chunks, one per child. Split points are adjusted forward to the next whitespace so no word gets cut in half.
3. Each child process is created with `fork()`. Children count word frequencies in their chunk and write results into a pre-allocated shared memory region (set up with `mmap`, using `MAP_SHARED | MAP_ANONYMOUS`).
4. Each child gets a dedicated slot in shared memory (a `ChildSlot` struct containing a count and a fixed-size array of word entries). This avoids any need for synchronization between children since they never touch each other's memory.
5. After all children exit, the parent walks through every slot and merges the results. Words that appeared in multiple chunks have their frequencies summed.
6. The merged results are sorted and the top 10 are printed.

## Building

Both programs target Linux or any POSIX-compliant system. You need `gcc`.

```
gcc -o naive naive.c
gcc -o multiprocess multiprocess.c
```

No external libraries required. Everything uses standard C and POSIX APIs (`fork`, `mmap`, `wait`).

## Usage

Single-process version:

```
./naive <input_file>
```

Multiprocess version:

```
./multiprocess <input_file> <num_processes>
```

The second argument is the number of child processes to create. A reasonable starting value is the number of CPU cores on your machine. The program caps this at 128.

Examples:

```
./naive war_and_peace.txt
./multiprocess war_and_peace.txt 4
```

## Example Output

```
Top 10 Most Frequent Words:
the: 34077
and: 21943
to: 16502
of: 14904
a: 10388
in: 8756
he: 7624
his: 7329
that: 7020
was: 6972

Execution time: 0.482351 seconds
```

Both programs produce the same output for the same input file.

## Project Structure

```
.
|-- naive.c              Single-process word frequency counter
|-- multiprocess.c       Multiprocess version using fork and shared memory
|-- ENCS3390_Project1_Fall2024.pdf   Original assignment specification
|-- OS-Report.docx       Project report document
|-- README.md
```

## Implementation Notes

**Word normalization.** Every token is converted to lowercase and has non-alphanumeric characters stripped from both ends before counting. So `"The"`, `"the"`, and `"the,"` all map to `the`.

**Dynamic array growth.** The naive version starts with space for 10 words and doubles capacity with `realloc` as needed. Memory usage stays proportional to the number of unique words.

**Merge sort.** Both versions sort descending by frequency. Temporary arrays are heap-allocated during each merge step and freed right after. Nothing fancy, just a standard top-down merge sort.

**Shared memory layout.** Each child writes into its own `ChildSlot` in a `MAP_SHARED` region. A slot holds an integer word count followed by a fixed array of up to 10,000 word-frequency pairs. Since each child has its own slot, there is no race condition and no locking needed during the counting phase.

**Chunk boundary adjustment.** Raw split points (file size / number of children) almost always land in the middle of a word. The `adjustBoundary` function shifts each split forward to the next whitespace character, so every child starts and ends on a clean word boundary.

**Result merging.** After all children finish, the parent iterates through each child's slot and builds a single combined frequency table. If the same word appears in multiple children's results (which happens whenever a word spans multiple chunks or just appears throughout the file), its counts are added together.

## Performance

The multiprocess version is faster on large files because the counting work runs in parallel. How much faster depends on the file size, how many unique words there are, and how many cores are available.

On small files (under a few hundred KB), the overhead of forking processes and setting up shared memory can make the multiprocess version slower. This is expected. The benefit only shows up when there is enough work to divide.

## Known Limitations

- Word lookup is a linear scan in both versions. A hash table would be significantly faster for files with large vocabularies, but that was outside the scope of this assignment.
- Each child process can track up to 10,000 unique words. If a chunk contains more unique words than that, a warning is printed to stderr and the extra words are dropped. For most real-world text this limit is not an issue.
- Only ASCII text is handled correctly. Multibyte UTF-8 characters will not be normalized properly.
- Tokenization splits on whitespace only (spaces, tabs, newlines). Characters like hyphens and slashes inside words are kept as-is, so `"well-known"` is treated as one token.
