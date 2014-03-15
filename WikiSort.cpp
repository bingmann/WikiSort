// -*- mode: c++; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-
/***********************************************************
 to run:
 clang++ -o WikiSort.x WikiSort.cpp -O3
 (or replace 'clang++' with 'g++')
 ./WikiSort.x
***********************************************************/

#include <iostream>
#include <algorithm>
#include <vector>
#include <cmath>
#include <cassert>
#include <cstring>

double Seconds() { return clock() * 1.0/CLOCKS_PER_SEC; }

// structure to represent ranges within the array
class Range {
public:
	long start;
	long end;
	
	Range() {}
	Range(long start, long end) : start(start), end(end) {}
	inline long length() const { return end - start; }
};

// structure to represent ranges within the array
template <typename IteratorType>
class RangeI {
public:
	typedef IteratorType iterator;
	typedef typename std::iterator_traits<iterator>::value_type value_type;

	iterator start;
	iterator end;
	
	RangeI() {}
	RangeI(iterator start, iterator end) : start(start), end(end) {}
	RangeI(value_type* array, const Range& r) : start(array + r.start), end(array + r.end) {}
	inline long length() const { return end - start; }
};

// toolbox functions used by the sorter

// 63 -> 32, 64 -> 64, etc.
// apparently this comes from Hacker's Delight?
long FloorPowerOfTwo (const long value) {
	long x = value;
	x = x | (x >> 1);
	x = x | (x >> 2);
	x = x | (x >> 4);
	x = x | (x >> 8);
	x = x | (x >> 16);
#if __LP64__
	x = x | (x >> 32);
#endif
	return x - (x >> 1);
}

// find the index of the first value within the range that is equal to array[index]
template <typename Iterator, typename Comparison>
long BinaryFirst(Iterator begin, Iterator end, const typename std::iterator_traits<Iterator>::value_type& value, const Comparison compare) {
	return std::lower_bound(begin, end, value, compare) - begin;
}

// find the index of the last value within the range that is equal to array[index], plus 1
template <typename Iterator, typename Comparison>
long BinaryLast(Iterator begin, Iterator end, const typename std::iterator_traits<Iterator>::value_type& value, const Comparison compare) {
	return std::upper_bound(begin, end, value, compare) - begin;
}

// n^2 sorting algorithm used to sort tiny chunks of the full array
template <typename Iterator, typename Comparison>
void InsertionSort(Iterator begin, Iterator end, const Comparison compare) {
	std::__insertion_sort(begin, end, compare);
}

// swap a series of values in the array
template <typename Iterator>
void BlockSwap(Iterator start1, Iterator start2, const long block_size) {
	std::swap_ranges(start1, start1 + block_size, start2);
}

// rotate the values in an array ([0 1 2 3] becomes [1 2 3 0] if we rotate by 1)
template <typename Iterator>
void Rotate(Iterator begin, Iterator end, const long amount, Iterator cache, const long cache_size) {
	if (begin >= end) return;
	
	Iterator split;
	if (amount >= 0) split = begin + amount;
	else split = end + amount;

	size_t r1 = split - begin;
	size_t r2 = end - split;
	
	// if the smaller of the two ranges fits into the cache, it's *slightly* faster copying it there and shifting the elements over
	if (r1 <= r2) {
		if (r1 <= cache_size) {
			std::copy(begin, split, cache);
			std::copy_backward(split, end, begin + r2);
			std::copy(cache, cache + r1, begin + r2);
			return;
		}
	} else {
		if (r2 <= cache_size) {
			std::copy(split, end, cache);
			std::copy_backward(begin, split, end);
			std::copy(cache, cache + r2, begin);
			return;
		}
	}

	std::rotate(begin, split, end);
}

namespace Wiki {
	// standard merge operation using an internal or external buffer
	template <typename Iterator, typename Comparison>
	void Merge(const RangeI<Iterator> buffer, const RangeI<Iterator> A, const RangeI<Iterator> B,
		   const Comparison compare, Iterator cache, const long cache_size) {
		// if A fits into the cache, use that instead of the internal buffer
		if (A.length() <= cache_size) {
			Iterator A_index = cache, B_index = B.start;
			Iterator A_last = cache + A.length(), B_last = B.end;
			Iterator insert_index = A.start;
			
			if (B.length() > 0 && A.length() > 0) {
				while (true) {
					if (!compare(*B_index, *A_index)) {
						*insert_index = *A_index;
						A_index++;
						insert_index++;
						if (A_index == A_last) break;
					} else {
						*insert_index = *B_index;
						B_index++;
						insert_index++;
						if (B_index == B_last) break;
					}
				}
			}
			
			// copy the remainder of A into the final array
			std::copy(A_index, A_last, insert_index);
		} else {
			// whenever we find a value to add to the final array, swap it with the value that's already in that spot
			// when this algorithm is finished, 'buffer' will contain its original contents, but in a different order
			Iterator A_index = buffer.start, B_index = B.start;
			Iterator A_last = buffer.start + A.length(), B_last = B.end;
			Iterator insert_index = A.start;
			
			if (B.length() > 0 && A.length() > 0) {
				while (true) {
					if (!compare(*B_index, *A_index)) {
						std::swap(*insert_index, *A_index);
						A_index++;
						insert_index++;
						if (A_index == A_last) break;
					} else {
						std::swap(*insert_index, *B_index);
						B_index++;
						insert_index++;
						if (B_index == B_last) break;
					}
				}
			}
			
			std::swap_ranges(A_index, A_last, insert_index);
		}
	}
	
	// standard merge operation using an internal or external buffer
	template <typename T, typename Comparison>
	void Merge(T array[], const Range buffer, const Range A, const Range B, const Comparison compare, T cache[], const long cache_size) {
		typedef RangeI<T*> RangeT;
		Merge(RangeT(array, buffer), RangeT(array, A), RangeT(array, B), compare, cache, cache_size);
	}

	// bottom-up merge sort combined with an in-place merge algorithm for O(1) memory use
	template <typename Iterator, typename Comparison>
	void Sort(Iterator first, Iterator last, const Comparison compare) {
		// map first and last to a C-style array, so we don't have to change the rest of the code
		// (bit of a nasty hack, but it's good enough for now...)
		const long size = last - first;
		__typeof__(&first[0]) array = &first[0];
		
		// if there are 32 or fewer items, just insertion sort the entire array
		if (size <= 32) {
			InsertionSort(array, array + size, compare);
			return;
		}
		
		// use a small cache to speed up some of the operations
		// since the cache size is fixed, it's still O(1) memory!
		// just keep in mind that making it too small ruins the point (nothing will fit into it),
		// and making it too large also ruins the point (so much for "low memory"!)
		// removing the cache entirely still gives 70% of the performance of a standard merge
		
		// also, if you change this to dynamically allocate a full-size buffer,
		// the algorithm seamlessly degenerates into a standard merge sort!
		const long cache_size = 512;
		__typeof__(array[0]) cache[cache_size];
		
		// calculate how to scale the index value to the range within the array
		// (this is essentially fixed-point math, where we manually check for and handle overflow)
		const long power_of_two = FloorPowerOfTwo(size);
		const long fractional_base = power_of_two/16;
		long fractional_step = size % fractional_base;
		long decimal_step = size/fractional_base;
		
		// first insertion sort everything the lowest level, which is 16-31 items at a time
		long start, mid, end, decimal = 0, fractional = 0;
		while (decimal < size) {
			start = decimal;
			
			decimal += decimal_step;
			fractional += fractional_step;
			if (fractional >= fractional_base) {
				fractional -= fractional_base;
				decimal++;
			}
			
			end = decimal;
			
			InsertionSort(array + start, array + end, compare);
		}
		
		// then merge sort the higher levels, which can be 32-63, 64-127, 128-255, etc.
		for (long merge_size = 16; merge_size < power_of_two; merge_size += merge_size) {
			long block_size = sqrt(decimal_step);
			long buffer_size = decimal_step/block_size + 1;
			
			// as an optimization, we really only need to pull out an internal buffer once for each level of merges
			// after that we can reuse the same buffer over and over, then redistribute it when we're finished with this level
			Range level1 = Range(0, 0), level2, levelA, levelB;
			
			decimal = fractional = 0;
			while (decimal < size) {
				start = decimal;
				
				decimal += decimal_step;
				fractional += fractional_step;
				if (fractional >= fractional_base) {
					fractional -= fractional_base;
					decimal++;
				}
				
				mid = decimal;
				
				decimal += decimal_step;
				fractional += fractional_step;
				if (fractional >= fractional_base) {
					fractional -= fractional_base;
					decimal++;
				}
				
				end = decimal;
				
				if (compare(array[end - 1], array[start])) {
					// the two ranges are in reverse order, so a simple rotation should fix it
					Rotate(array + start, array + end, mid - start, cache, cache_size);
					
				} else if (compare(array[mid], array[mid - 1])) {
					// these two ranges weren't already in order, so we'll need to merge them!
					Range A = Range(start, mid), B = Range(mid, end);
					
					if (A.length() <= cache_size) {
						std::copy(&array[A.start], &array[A.end], &cache[0]);
						Merge(array, Range(0, 0), A, B, compare, cache, cache_size);
						continue;
					}
					
					// try to fill up two buffers with unique values in ascending order
					Range bufferA, bufferB, buffer1, buffer2, blockA, blockB, firstA, lastA, lastB;
					
					if (level1.length() > 0) {
						// reuse the buffers we found in a previous iteration
						bufferA = Range(A.start, A.start);
						bufferB = Range(B.end, B.end);
						buffer1 = level1;
						buffer2 = level2;
						
					} else {
						// the first item is always going to be the first unique value, so let's start searching at the next index
						long count = 1;
						for (buffer1.start = A.start + 1; buffer1.start < A.end; buffer1.start++)
							if (compare(array[buffer1.start - 1], array[buffer1.start]) || compare(array[buffer1.start], array[buffer1.start - 1]))
								if (++count == buffer_size)
									break;
						buffer1.end = buffer1.start + count;
						
						// if the size of each block fits into the cache, we only need one buffer for tagging the A blocks
						// this is because the other buffer is used as a swap space for merging the A blocks into the B values that follow it,
						// but we can just use the cache as the buffer instead. this skips some memmoves and an insertion sort
						if (buffer_size <= cache_size) {
							buffer2 = Range(A.start, A.start);
							
							if (buffer1.length() == buffer_size) {
								// we found enough values for the buffer in A
								bufferA = Range(buffer1.start, buffer1.start + buffer_size);
								bufferB = Range(B.end, B.end);
								buffer1 = Range(A.start, A.start + buffer_size);
								
							} else {
								// we were unable to find enough unique values in A, so try B
								bufferA = Range(buffer1.start, buffer1.start);
								buffer1 = Range(A.start, A.start);
								
								// the last value is guaranteed to be the first unique value we encounter, so we can start searching at the next index
								count = 1;
								for (buffer1.start = B.end - 2; buffer1.start >= B.start; buffer1.start--)
									if (compare(array[buffer1.start], array[buffer1.start + 1]) || compare(array[buffer1.start + 1], array[buffer1.start]))
										if (++count == buffer_size)
											break;
								buffer1.end = buffer1.start + count;
								
								if (buffer1.length() == buffer_size) {
									bufferB = Range(buffer1.start, buffer1.start + buffer_size);
									buffer1 = Range(B.end - buffer_size, B.end);
								}
							}
						} else {
							// the first item of the second buffer isn't guaranteed to be the first unique value, so we need to find the first unique item too
							count = 0;
							for (buffer2.start = buffer1.start + 1; buffer2.start < A.end; buffer2.start++)
								if (compare(array[buffer2.start - 1], array[buffer2.start]) || compare(array[buffer2.start], array[buffer2.start - 1]))
									if (++count == buffer_size)
										break;
							buffer2.end = buffer2.start + count;
							
							if (buffer2.length() == buffer_size) {
								// we found enough values for both buffers in A
								bufferA = Range(buffer2.start, buffer2.start + buffer_size * 2);
								bufferB = Range(B.end, B.end);
								buffer1 = Range(A.start, A.start + buffer_size);
								buffer2 = Range(A.start + buffer_size, A.start + buffer_size * 2);
								
							} else if (buffer1.length() == buffer_size) {
								// we found enough values for one buffer in A, so we'll need to find one buffer in B
								bufferA = Range(buffer1.start, buffer1.start + buffer_size);
								buffer1 = Range(A.start, A.start + buffer_size);
								
								// like before, the last value is guaranteed to be the first unique value we encounter, so we can start searching at the next index
								count = 1;
								for (buffer2.start = B.end - 2; buffer2.start >= B.start; buffer2.start--)
									if (compare(array[buffer2.start], array[buffer2.start + 1]) || compare(array[buffer2.start + 1], array[buffer2.start]))
										if (++count == buffer_size)
											break;
								buffer2.end = buffer2.start + count;
								
								if (buffer2.length() == buffer_size) {
									bufferB = Range(buffer2.start, buffer2.start + buffer_size);
									buffer2 = Range(B.end - buffer_size, B.end);
									
								} else buffer1.end = buffer1.start; // failure
							} else {
								// we were unable to find a single buffer in A, so we'll need to find two buffers in B
								count = 1;
								for (buffer1.start = B.end - 2; buffer1.start >= B.start; buffer1.start--)
									if (compare(array[buffer1.start], array[buffer1.start + 1]) || compare(array[buffer1.start + 1], array[buffer1.start]))
										if (++count == buffer_size)
											break;
								buffer1.end = buffer1.start + count;
								
								count = 0;
								for (buffer2.start = buffer1.start - 1; buffer2.start >= B.start; buffer2.start--)
									if (compare(array[buffer2.start], array[buffer2.start + 1]) || compare(array[buffer2.start + 1], array[buffer2.start]))
										if (++count == buffer_size)
											break;
								buffer2.end = buffer2.start + count;
								
								if (buffer2.length() == buffer_size) {
									bufferA = Range(A.start, A.start);
									bufferB = Range(buffer2.start, buffer2.start + buffer_size * 2);
									buffer1 = Range(B.end - buffer_size, B.end);
									buffer2 = Range(buffer1.start - buffer_size, buffer1.start);
									
								} else buffer1.end = buffer1.start; // failure
							}
						}
						
						if (buffer1.length() < buffer_size) {
							// we failed to fill both buffers with unique values, which implies we're merging two subarrays with a lot of the same values repeated
							// we can use this knowledge to write a merge operation that is optimized for arrays of repeating values
							while (A.length() > 0 && B.length() > 0) {
								// find the first place in B where the first item in A needs to be inserted
								long mid = BinaryFirst(array + B.start, array + B.end, array[A.start], compare) + B.start;
								
								// rotate A into place
								long amount = mid - A.end;
								Rotate(array + A.start, array + mid, -amount, cache, cache_size);
								
								// calculate the new A and B ranges
								B.start = mid;
								A = Range(BinaryLast(array + A.start, array + A.end, array[A.start + amount], compare) + A.start, B.start);
							}
							
							continue;
						}
						
						// move the unique values to the start of A if needed
						long length = bufferA.length(); count = 0;
						for (long index = bufferA.start; count < length; index--) {
							if (index == A.start || compare(array[index - 1], array[index]) || compare(array[index], array[index - 1])) {
								Rotate(array + index + 1, array + bufferA.start + 1, -count, cache, cache_size);
								bufferA.start = index + count; count++;
							}
						}
						bufferA = Range(A.start, A.start + length);
						
						// move the unique values to the end of B if needed
						length = bufferB.length(); count = 0;
						for (long index = bufferB.start; count < length; index++) {
							if (index == B.end - 1 || compare(array[index], array[index + 1]) || compare(array[index + 1], array[index])) {
								Rotate(array + bufferB.start, array + index, count, cache, cache_size);
								bufferB.start = index - count; count++;
							}
						}
						bufferB = Range(B.end - length, B.end);
						
						// reuse these buffers next time!
						level1 = buffer1;
						level2 = buffer2;
						levelA = bufferA;
						levelB = bufferB;
					}
					
					// break the remainder of A into blocks. firstA is the uneven-sized first A block
					blockA = Range(bufferA.end, A.end);
					firstA = Range(bufferA.end, bufferA.end + blockA.length() % block_size);
					
					// swap the second value of each A block with the value in buffer1
					for (long index = 0, indexA = firstA.end + 1; indexA < blockA.end; index++, indexA += block_size) 
						std::swap(array[buffer1.start + index], array[indexA]);
					
					// start rolling the A blocks through the B blocks!
					// whenever we leave an A block behind, we'll need to merge the previous A block with any B blocks that follow it, so track that information as well
					lastA = firstA;
					lastB = Range(0, 0);
					blockB = Range(B.start, B.start + std::min(block_size, B.length() - bufferB.length()));
					blockA.start += firstA.length();
					
					long minA = blockA.start, indexA = 0;
					__typeof__(*array) min_value = array[minA];
					
					if (lastA.length() <= cache_size)
						std::copy(&array[lastA.start], &array[lastA.end], &cache[0]);
					else
						BlockSwap(array + lastA.start, array + buffer2.start, lastA.length());
					
					while (true) {
						// if there's a previous B block and the first value of the minimum A block is <= the last value of the previous B block,
						// then drop that minimum A block behind. or if there are no B blocks left then keep dropping the remaining A blocks.
						if ((lastB.length() > 0 && !compare(array[lastB.end - 1], min_value)) || blockB.length() == 0) {
							// figure out where to split the previous B block, and rotate it at the split
							long B_split = BinaryFirst(array + lastB.start, array + lastB.end, min_value, compare) + lastB.start;
							long B_remaining = lastB.end - B_split;
							
							// swap the minimum A block to the beginning of the rolling A blocks
							BlockSwap(array + blockA.start, array + minA, block_size);
							
							// we need to swap the second item of the previous A block back with its original value, which is stored in buffer1
							// since the firstA block did not have its value swapped out, we need to make sure the previous A block is not unevenly sized
							std::swap(array[blockA.start + 1], array[buffer1.start + indexA++]);
							
							// locally merge the previous A block with the B values that follow it, using the buffer as swap space
							Merge(array, buffer2, lastA, Range(lastA.end, B_split), compare, cache, cache_size);
							
							// copy the previous A block into the cache or buffer2, since that's where we need it to be when we go to merge it anyway
							if (block_size <= cache_size)
								std::copy(&array[blockA.start], &array[blockA.start + block_size], cache);
							else
								BlockSwap(array + blockA.start, array + buffer2.start, block_size);
							
							// this is equivalent to rotating, but faster
							// the area normally taken up by the A block is either the contents of buffer2, or data we don't need anymore since we memcopied it
							// either way, we don't need to retain the order of those items, so instead of rotating we can just block swap B to where it belongs
							BlockSwap(array + B_split, array + blockA.start + block_size - B_remaining, B_remaining);
							
							// now we need to update the ranges and stuff
							lastA = Range(blockA.start - B_remaining, blockA.start - B_remaining + block_size);
							lastB = Range(lastA.end, lastA.end + B_remaining);
							
							blockA.start += block_size;
							if (blockA.length() == 0)
								break;
							
							// search the second value of the remaining A blocks to find the new minimum A block (that's why we wrote unique values to them!)
							minA = blockA.start + 1;
							for (long findA = minA + block_size; findA < blockA.end; findA += block_size)
								if (compare(array[findA], array[minA])) minA = findA;
							minA = minA - 1; // decrement once to get back to the start of that A block
							min_value = array[minA];
							
						} else if (blockB.length() < block_size) {
							// move the last B block, which is unevenly sized, to before the remaining A blocks, by using a rotation
							// (using the cache is disabled since we have the contents of the previous A block in it!)
							Rotate(array + blockA.start, array + blockB.end, -blockB.length(), cache, 0);
							lastB = Range(blockA.start, blockA.start + blockB.length());
							blockA.start += blockB.length();
							blockA.end += blockB.length();
							minA += blockB.length();
							blockB.end = blockB.start;
						} else {
							// roll the leftmost A block to the end by swapping it with the next B block
							BlockSwap(array + blockA.start, array + blockB.start, block_size);
							lastB = Range(blockA.start, blockA.start + block_size);
							if (minA == blockA.start)
								minA = blockA.end;
							
							blockA.start += block_size;
							blockA.end += block_size;
							blockB.start += block_size;
							blockB.end += block_size;
							
							if (blockB.end > bufferB.start)
								blockB.end = bufferB.start;
						}
					}
					
					// merge the last A block with the remaining B blocks
					Merge(array, buffer2, lastA, Range(lastA.end, B.end - bufferB.length()), compare, cache, cache_size);
				}
			}
			
			if (level1.length() > 0) {
				// when we're finished with this step we should have b1 b2 left over, where one of the buffers is all jumbled up
				// insertion sort the jumbled up buffer, then redistribute them back into the array using the opposite process used for creating the buffer
				InsertionSort(array + level2.start, array + level2.end, compare);
				
				// redistribute bufferA back into the array
				long level_start = levelA.start;
				for (long index = levelA.end; levelA.length() > 0; index++) {
					if (index == levelB.start || !compare(array[index], array[levelA.start])) {
						long amount = index - (levelA.end);
						Rotate(array + levelA.start, array + index, -amount, cache, cache_size);
						levelA.start += (amount + 1);
						levelA.end += amount;
						index--;
					}
				}
				
				// redistribute bufferB back into the array
				for (long index = levelB.start; levelB.length() > 0; index--) {
					if (index == level_start || !compare(array[levelB.end - 1], array[index - 1])) {
						long amount = levelB.start - index;
						Rotate(array + index, array + levelB.end, amount, cache, cache_size);
						levelB.start -= amount;
						levelB.end -= (amount + 1);
						index++;
					}
				}
			}
			
			decimal_step += decimal_step;
			fractional_step += fractional_step;
			if (fractional_step >= fractional_base) {
				fractional_step -= fractional_base;
				decimal_step++;
			}
		}
	}
}


// structure to test stable sorting (index will contain its original index in the array, to make sure it doesn't switch places with other items)
typedef struct {
	int value;
	int index;
} Test;

bool TestCompare(Test item1, Test item2) { return (item1.value < item2.value); }

namespace Testing {
	long Pathological(long index, long total) {
		if (index == 0) return 10;
		else if (index < total/2) return 11;
		else if (index == total - 1) return 10;
		return 9;
	}
	
	// purely random data is one of the few cases where it is slower than stable_sort(),
	// although it does end up only running at about 87% as fast in that situation
	long Random(long /* index */ , long /* total */) {
		return rand();
	}
	
	long MostlyDescending(long index, long total) {
		return total - index + rand() * 1.0/RAND_MAX * 5 - 2.5;
	}
	
	long MostlyAscending(long index, long /* total */) {
		return index + rand() * 1.0/RAND_MAX * 5 - 2.5;
	}
	
	long Ascending(long index, long /* total */) {
		return index;
	}
	
	long Descending(long index, long total) {
		return total - index;
	}
	
	long Equal(long /* index */, long /* total */) {
		return 1000;
	}
	
	long Jittered(long index, long /* total */) {
		return (rand() * 1.0/RAND_MAX <= 0.9) ? index : (index - 2);
	}
	
	long MostlyEqual(long /* index */, long /* total */) {
		return 1000 + rand() * 1.0/RAND_MAX * 4;
	}
}

using namespace std;

// make sure the items within the given range are in a stable order
template <typename Iterator, typename Comparison>
void Verify(Iterator start, Iterator end, const Comparison compare, const string msg) {
	for (Iterator iter = start + 1; iter < end; iter++) {
		// if it's in ascending order then we're good
		// if both values are equal, we need to make sure the index values are ascending
		if (!(compare(*(iter - 1), *iter) ||
		      (!compare(*iter, *(iter - 1)) && iter->index > (iter - 1)->index))) {
			
			for (Iterator iter2 = start; iter2 < end; iter2++)
				cout << iter2->value << " (" << iter2->index << ") ";
			
			cout << endl << "failed with message: " << msg << endl;
			assert(false);
		}
	}
}

int main() {
	const long max_size = 1500000;
	__typeof__(&TestCompare) compare = &TestCompare;
	vector<Test> array1, array2;
	
	__typeof__(&Testing::Pathological) test_cases[] = {
		Testing::Pathological,
		Testing::Random,
		Testing::MostlyDescending,
		Testing::MostlyAscending,
		Testing::Ascending,
		Testing::Descending,
		Testing::Equal,
		Testing::Jittered,
		Testing::MostlyEqual
	};
	
	// initialize the random-number generator
	srand(time(NULL));
	//srand(10141985); // in case you want the same random numbers
	
	cout << "running test cases... " << flush;
	long total = max_size;
	array1.resize(total);
	array2.resize(total);
	for (size_t test_case = 0; test_case < sizeof(test_cases)/sizeof(test_cases[0]); test_case++) {
		
		for (long index = 0; index < total; index++) {
			Test item;
			
			item.value = test_cases[test_case](index, total);
			item.index = index;
			
			array1[index] = array2[index] = item;
		}
		
		Wiki::Sort(array1.begin(), array1.end(), compare);
		
		stable_sort(array2.begin(), array2.end(), compare);
		
		Verify(array1.begin(), array1.end(), compare, "test case failed");
		for (long index = 0; index < total; index++)
			assert(!compare(array1[index], array2[index]) && !compare(array2[index], array1[index]));
	}
	cout << "passed!" << endl;
	
	double total_time = Seconds();
	double total_time1 = 0, total_time2 = 0;
	
	for (total = 0; total < max_size; total += 2048 * 16) {
		array1.resize(total);
		array2.resize(total);
		
		for (long index = 0; index < total; index++) {
			Test item;
			
			//Testing::Pathological,
			//Testing::Random,
			//Testing::MostlyDescending,
			//Testing::MostlyAscending,
			//Testing::Ascending,
			//Testing::Descending,
			//Testing::Equal,
			//Testing::Jittered,
			//Testing::MostlyEqual
			
			item.value = Testing::Random(index, total);
			item.index = index;
			
			array1[index] = array2[index] = item;
		}
		
		double time1 = Seconds();
		Wiki::Sort(array1.begin(), array1.end(), compare);
		time1 = Seconds() - time1;
		total_time1 += time1;
		
		double time2 = Seconds();
		//__inplace_stable_sort(array2.begin(), array2.end(), compare);
		stable_sort(array2.begin(), array2.end(), compare);
		time2 = Seconds() - time2;
		total_time2 += time2;
		
		cout << "[" << total << "] wiki: " << time1 << ", merge: " << time2 << " (" << time2/time1 * 100.0 << "%)" << endl;
		
		// make sure the arrays are sorted correctly, and that the results were stable
		cout << "verifying... " << flush;
		
		Verify(array1.begin(), array1.end(), compare, "testing the final array");
		for (long index = 0; index < total; index++)
			assert(!compare(array1[index], array2[index]) && !compare(array2[index], array1[index]));
		
		cout << "correct!" << endl;
	}
	
	total_time = Seconds() - total_time;
	cout << "tests completed in " << total_time << " seconds" << endl;
	cout << "wiki: " << total_time1 << ", merge: " << total_time2 << " (" << total_time2/total_time1 * 100.0 << "%)" << endl;
	
	return 0;
}
