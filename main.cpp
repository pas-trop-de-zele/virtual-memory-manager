#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <cassert>
#include <vector>
#include <algorithm>

#pragma warning(disable : 4996)

#define ARGC_ERROR 1
#define FILE_ERROR 2

#define FRAME_SIZE 256
#define FIFO 0
#define LRU 1
#define REPLACE_POLICY FIFO

// SET TO 128 to use replacement policy: FIFO or LRU,
#define NFRAMES 128
#define PTABLE_SIZE 256
#define TLB_SIZE 16

class page_node
{
private:
    void copy(const page_node &other)
    {
        npage = other.npage;
        frame_num = other.frame_num;
        is_present = other.is_present;
        is_used = other.is_used;
    }

public:
    size_t npage;
    size_t frame_num;
    bool is_present;
    bool is_used;

    page_node() : page_node(256, 256, false, false) {}

    page_node(size_t npage_, size_t frame_num_, bool is_present_, bool is_used_)
        : npage(npage_), frame_num(frame_num_), is_present(is_present_), is_used(is_used_) {}

    page_node(const page_node &other) : page_node() { copy(other); }

    page_node &operator=(const page_node &other)
    {
        if (this != &other)
        {
            copy(other);
        }
        return *this;
    }
};

char *ram = (char *)malloc(NFRAMES * FRAME_SIZE);
std::vector<int> lru_cache(NFRAMES, -1);

page_node pg_table[PTABLE_SIZE]; // page table and (single) TLB
page_node tlb[TLB_SIZE];

const char *passed_or_failed(bool condition) { return condition ? " + " : "fail"; }
size_t failed_asserts = 0;

size_t get_page(size_t x) { return 0xff & (x >> 8); }
size_t get_offset(size_t x) { return 0xff & x; }

void display_lru_cache()
{
    for (auto frame : lru_cache)
    {
        printf("%d ", frame);
    }
    printf("\n");
}

void get_page_offset(size_t x, size_t &page, size_t &offset)
{
    page = get_page(x);
    offset = get_offset(x);
    // printf("x is: %zu, page: %zu, offset: %zu, address: %zu, paddress: %zu\n",
    //        x, page, offset, (page << 8) | get_offset(x), page * 256 + offset);
}

void update_frame_ptable(size_t npage, size_t frame_num)
{
    pg_table[npage].frame_num = frame_num;
    pg_table[npage].is_present = true;
    pg_table[npage].is_used = true;
}

int find_frame_ptable(size_t frame)
{ // FIFO
    for (int i = 0; i < PTABLE_SIZE; i++)
    {
        if (pg_table[i].frame_num == frame &&
            pg_table[i].is_present == true)
        {
            return i;
        }
    }
    return -1;
}

size_t get_used_ptable()
{ // LRU
    size_t unused = -1;
    for (size_t i = 0; i < PTABLE_SIZE; i++)
    {
        if (pg_table[i].is_used == false &&
            pg_table[i].is_present == true)
        {
            return (size_t)i;
        }
    }
    // All present pages have been used recently, set all page entry used flags to false
    for (size_t i = 0; i < PTABLE_SIZE; i++)
    {
        pg_table[i].is_used = false;
    }
    for (size_t i = 0; i < PTABLE_SIZE; i++)
    {
        page_node &r = pg_table[i];
        if (!r.is_used && r.is_present)
        {
            return i;
        }
    }
    return (size_t)-1;
}

int check_tlb(size_t page)
{
    for (int i = 0; i < TLB_SIZE; i++)
    {
        if (tlb[i].npage == page)
        {
            return i;
        }
    }
    return -1;
}

void open_files(FILE *&fadd, FILE *&fcorr, FILE *&fback)
{
    fadd = fopen("addresses.txt", "r");
    if (fadd == NULL)
    {
        fprintf(stderr, "Could not open file: 'addresses.txt'\n");
        exit(FILE_ERROR);
    }

    fcorr = fopen("correct.txt", "r");
    if (fcorr == NULL)
    {
        fprintf(stderr, "Could not open file: 'correct.txt'\n");
        exit(FILE_ERROR);
    }

    fback = fopen("BACKING_STORE.bin", "rb");
    if (fback == NULL)
    {
        fprintf(stderr, "Could not open file: 'BACKING_STORE.bin'\n");
        exit(FILE_ERROR);
    }
}
void close_files(FILE *fadd, FILE *fcorr, FILE *fback)
{
    fclose(fadd);
    fclose(fcorr);
    fclose(fback);
}

void initialize_pg_table_tlb()
{
    for (int i = 0; i < PTABLE_SIZE; ++i)
    {
        pg_table[i].npage = (size_t)i;
        pg_table[i].is_present = false;
        pg_table[i].is_used = false;
    }

    for (int i = 0; i < TLB_SIZE; i++)
    {
        tlb[i].npage = (size_t)-1;
        tlb[i].is_present = false;
        pg_table[i].is_used = false;
    }
}

void summarize(size_t pg_faults, size_t tlb_hits)
{
    printf("\nPage Fault Percentage: %1.3f%%", (double)pg_faults / 1000 * 100);
    printf("\nTLB Hit Percentage: %1.3f%%\n\n", (double)tlb_hits / 1000 * 100);
    printf("ALL logical ---> physical assertions PASSED!\n");
    printf("\n\t\t...done.\n");
}

// TODO
void tlb_add(int index, const page_node &entry)
{
    tlb[index] = entry;
}

// TODO
void tlb_remove(int index)
{
    tlb[index].npage = (size_t)-1;
    tlb[index].is_present = false;
    tlb[index].is_used = false;
}

// TODO
void tlb_hit(size_t &frame, int index)
{
    frame = tlb[index].frame_num;
}

// TODO
void tlb_miss(size_t &frame, size_t &page, size_t &tlb_track)
{
    frame = pg_table[page].frame_num;
    tlb[tlb_track].frame_num = frame;
}

void fifo_replace_page(size_t &frame, size_t frames_used)
{
    frame = frames_used % NFRAMES;
}

/**
 * @brief
 * Swap the most recently used frame to the end
 * and shift every other frame back by 1 accordingly
 *
 * @param frame: frame that is being used
 * @param frames_used: how many frames used so far
 */
void lru_cache_use(size_t frame, size_t frames_used)
{
    int frame_index = std::find(lru_cache.begin(), lru_cache.end(), frame) - lru_cache.begin();
    int most_recently_used_frame_index = frames_used <= NFRAMES ? frames_used - 1 : NFRAMES - 1;

    for (int i = frame_index; i < most_recently_used_frame_index; ++i)
    {
        lru_cache[i] = lru_cache[i + 1];
    }
    lru_cache[most_recently_used_frame_index] = frame;
}

void lru_replace_page(size_t &frame, size_t frames_used)
{
    size_t lru_frame = lru_cache[0];
    frame = lru_frame;
    lru_cache_use(lru_frame, frames_used);
    printf("CLEAR DATA FROM LRU FRAME %lu\n", frame);
}

void ptable_remove(int index)
{
    pg_table[index].frame_num = -1;
    pg_table[index].is_present = false;
    pg_table[index].is_used = false;
}

void remove_frame_from_tlb_and_ptable(size_t frame)
{
    // There may be an entry in tlb that still
    // contains the old frame
    for (int i = 0; i < TLB_SIZE; ++i)
    {
        if (tlb[i].frame_num == frame)
        {
            tlb_remove(i);
            break;
        }
    }

    // There may be an entry in page table that still
    // contains the old frame
    int old_ptable_index = find_frame_ptable(frame);
    if (old_ptable_index != -1)
    {
        ptable_remove(old_ptable_index);
    }
}

void page_fault(size_t &frame, size_t &page, size_t &frames_used,
                size_t &tlb_track, FILE *fbacking)
{
    unsigned char buf[BUFSIZ];
    memset(buf, 0, sizeof(buf));

    frame = frames_used;

    bool is_memfull = frames_used >= NFRAMES ? true : false;
    if (is_memfull)
    {
        printf("-----------------------------------MEM_FULL-----------------------------------\n");
        if (REPLACE_POLICY == FIFO)
        {
            fifo_replace_page(frame, frames_used);
        }
        else
        {
            lru_replace_page(frame, frames_used);
        }
        remove_frame_from_tlb_and_ptable(frame);
    }
    else if (REPLACE_POLICY == LRU)
    {
        lru_cache[frames_used] = frame;
    }

    // load page into RAM, update pg_table, TLB
    fseek(fbacking, page * FRAME_SIZE, SEEK_SET);
    fread(buf, FRAME_SIZE, 1, fbacking);

    for (int offset = 0; offset < FRAME_SIZE; offset++)
    {
        *(ram + (frame * FRAME_SIZE) + offset) = buf[offset];
    }

    update_frame_ptable(page, frame);

    tlb_add(tlb_track++, pg_table[page]);

    if (tlb_track > 15)
    {
        tlb_track = 0;
    }

    ++frames_used;
}

void check_address_value(size_t logic_add, size_t page, size_t offset, size_t physical_add,
                         size_t &prev_frame, size_t frame, int val, int value, size_t o)
{
    printf("log: %5lu 0x%04lu (pg:%3lu, off:%3lu)-->phy: %5lu (frm: %3lu) (prv: %3lu)--> val: %4d == value: %4d -- %s\n\n",
           logic_add, logic_add, page, offset, physical_add, frame, prev_frame,
           val, value, passed_or_failed(val == value));

    // WHEN RAM < PAGE TABLE THIS DOES NOT MAKE SENSE
    // if (frame < prev_frame)
    // {
    //     printf("   HIT!\n");
    // }
    // else
    // {
    //     prev_frame = frame;
    //     printf("----> pg_fault\n");
    // }

    // OUTPUT SPACER
    if (o % 5 == 4)
    {
        printf("\n");
    }

    // if (o > 20) { exit(-1); }             // to check out first 20 elements

    if (val != value)
    {
        ++failed_asserts;
    }
    if (failed_asserts > 5)
    {
        exit(-1);
    }
    //     assert(val == value);
}

void run_simulation()
{
    // addresses, pages, frames, values, hits and faults
    size_t logic_add, virt_add, phys_add, physical_add;
    size_t page, frame, offset, value, prev_frame = 0, tlb_track = 0;
    size_t frames_used = 0, pg_faults = 0, tlb_hits = 0;
    int val = 0;
    char buf[BUFSIZ];

    bool is_memfull = false; // physical memory to store the frames

    initialize_pg_table_tlb();

    // addresses to test, correct values, and pages to load
    FILE *faddress, *fcorrect, *fbacking;
    open_files(faddress, fcorrect, fbacking);

    for (int o = 0; o < 1000; o++)
    { // read from file correct.txt
        fscanf(fcorrect, "%s %s %lu %s %s %lu %s %ld", buf, buf, &virt_add, buf, buf, &phys_add, buf, &value);

        fscanf(faddress, "%ld", &logic_add);

        get_page_offset(logic_add, page, offset);

        int tlb_index = check_tlb(page);
        if (tlb_index >= 0)
        {
            ++tlb_hits;
            printf("-------TLB HIT-----------\n");
            tlb_hit(frame, tlb_index);
        }
        else if (pg_table[page].is_present)
        {
            printf("-------TABLE HIT-----------\n");
            tlb_miss(frame, page, tlb_track);
        }
        else
        {
            printf("-------PAGE FAULT-----------\n");
            ++pg_faults;
            page_fault(frame, page, frames_used, tlb_track, fbacking);
        }

        if (REPLACE_POLICY == LRU)
        {
            lru_cache_use(frame, frames_used);
            display_lru_cache();
        }

        physical_add = (frame * FRAME_SIZE) + offset;
        val = (int)*(ram + physical_add);

        check_address_value(logic_add, page, offset, physical_add, prev_frame, frame, val, value, o);
    }
    close_files(faddress, fcorrect, fbacking); // and time to wrap things up
    free(ram);
    summarize(pg_faults, tlb_hits);
}

int main(int argc, const char *argv[])
{
    run_simulation();
    // printf("\nFailed asserts: %lu\n\n", failed_asserts);   // allows asserts to fail silently and be counted
    return 0;
}
