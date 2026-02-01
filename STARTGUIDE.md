# Getting Started with swiftSearch

## Prerequisites

Before using swiftSearch, ensure you have the following installed:

- **Operating System**: UNIX-based system (Linux, macOS, BSD)
- **Compiler**: C++20 compatible compiler
  - GCC 10+ or Clang 10+
- **Build Tools**: Make or CMake
- **Standard Library**: Full C++20 standard library support with threading

## Installation

### 1. Clone or Download the Repository

```bash
git clone <repository-url>
cd swiftSearch
```

### 2. Build the Project

```bash
# If using Make
make

# If using CMake
mkdir build
cd build
cmake ..
make
```

The compiled executable will be located in the `bin/` directory.

### 3. Verify Installation

```bash
./bin/swiftSearch
```

You should see the help information displaying available commands and flags.

## Basic Usage

swiftSearch follows this command structure:

```bash
./bin/swiftSearch #operation @filepath [flags]
```

### Command Structure

- `#operation` - The operation to perform (required)
- `@filepath` - Path to file or directory to analyze (required)
- `[flags]` - Optional flags to modify behavior

## Available Operations

### File Information
Display basic information about files in a directory:

```bash
./bin/swiftSearch #info /path/to/directory
```

**Output**: Filename, size, and full path for each file

---

### Number Operations

#### List Numbers
Extract and list all numbers from files in the order they appear:

```bash
./bin/swiftSearch #listnumbers /path/to/directory
```

**Output**: `ListedNumbers.txt` containing all numbers found

#### Sorted List
Extract and sort all numbers (smallest to largest):

```bash
./bin/swiftSearch #sortedlist /path/to/directory
```

**Output**: `SortedList.txt` with numbers in ascending order

#### Reverse Sorted List
Extract and sort all numbers (largest to smallest):

```bash
./bin/swiftSearch #-sortedlist /path/to/directory
```

**Output**: `ReverseSortedList.txt` with numbers in descending order

---

### Text Analysis

#### Character Frequency
Count the frequency of each character across all files:

```bash
./bin/swiftSearch #getcharacters /path/to/directory
```

**Output**: Character-by-character frequency counts printed to console

#### Word Frequency
Count the frequency of each word across all files:

```bash
./bin/swiftSearch #getwords /path/to/directory
```

**Output**: `Words.txt` containing words and their frequencies

---

### Search Operations

#### Find One
Search for the first occurrence of a word and stop:

```bash
./bin/swiftSearch #findone /path/to/directory
```

You will be prompted to enter the word to search for.

#### Find All
Search for all occurrences of a word across all files:

```bash
./bin/swiftSearch #findall /path/to/directory
```

You will be prompted to enter the word to search for.

**Output**: Console output showing each instance found with filename and line number

## Available Flags

Flags modify the behavior of operations and can be combined:

### `-debug`
Enable debug mode to see detailed runtime information:

```bash
./bin/swiftSearch #sortedlist /path/to/directory -debug
```

**Shows**: Operation progress, thread information, file processing details

### `-threadinfo`
Display thread entry/exit messages:

```bash
./bin/swiftSearch #getwords /path/to/directory -threadinfo
```

**Shows**: When threads start and finish processing each file

### `-caps`
Ignore capitalization in character and word operations:

```bash
./bin/swiftSearch #getwords /path/to/directory -caps
```

**Effect**: 'A' and 'a' are treated as the same character/word

### `-floatlist`
Use floating-point numbers instead of integers for number operations:

```bash
./bin/swiftSearch #sortedlist /path/to/directory -floatlist
```

**Effect**: Processes numbers as `long double` instead of `long long int`

## Examples

### Example 1: Analyze a Log Directory

Find all instances of "ERROR" in log files:

```bash
./bin/swiftSearch #findall /var/log/myapp -debug
```

### Example 2: Extract and Sort Data

Sort numbers from CSV files, ignoring case:

```bash
./bin/swiftSearch #sortedlist /data/csv_files -caps -floatlist
```

### Example 3: Word Frequency Analysis

Count word frequencies in text documents (case-insensitive):

```bash
./bin/swiftSearch #getwords /path/to/documents -caps -threadinfo
```

Output will be saved to `Words.txt` in the current directory.

### Example 4: Quick File Overview

Get information about all files in a directory:

```bash
./bin/swiftSearch #info ~/Desktop/project
```

## Understanding Output Files

swiftSearch generates different output files depending on the operation:

| Operation | Output File | Contents |
|-----------|-------------|----------|
| `#listnumbers` | `ListedNumbers.txt` | Numbers in order found |
| `#sortedlist` | `SortedList.txt` | Numbers ascending |
| `#-sortedlist` | `ReverseSortedList.txt` | Numbers descending |
| `#getwords` | `Words.txt` | Words with frequency counts |

**Note**: Output files are created in the current working directory.

## Performance Tips

1. **Use threading wisely**: The program automatically creates one thread per file. For directories with thousands of files, this may consume significant resources.

2. **Debug mode**: Only use `-debug` and `-threadinfo` flags when troubleshooting, as they add overhead.

3. **Large files**: For very large files (>1GB), operations may take significant time and memory.

4. **Number operations**: Use `-floatlist` only when you need decimal precision, as integer operations are faster.

## Troubleshooting

### "No command detected"
- Ensure you're using the correct format: `#operation @filepath`
- Operations must start with `#`
- Check that the operation name is spelled correctly

### "File did not open"
- Verify the filepath exists and is accessible
- Check file permissions (you need read access)
- Use absolute paths if relative paths aren't working

### No output generated
- Check that files in the directory contain the expected data type (numbers for number operations, text for word operations)
- Enable `-debug` flag to see what's happening during execution

### Program hangs/doesn't finish
- Check that files aren't locked by another process
- For very large directories, the program may simply be processing - enable `-threadinfo` to see progress

## Next Steps

- Explore combining multiple flags for customized analysis
- Try different operations on the same dataset to gain different insights
- Use output files as input for further processing or visualization

## Getting Help

For more detailed information about specific operations or flags, refer to the full documentation or source code comments.