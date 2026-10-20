#include "theatre.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- linked list of seats (one row) ---------- */

static SeatNode *node_new(char row, int seatNo) {
    SeatNode *n = calloc(1, sizeof *n);
    if (!n) { perror("calloc"); exit(1); }
    n->row = row;
    n->seatNo = seatNo;
    n->bookingId = 0;
    n->next = NULL;
    return n;
}

/* O(1) because we keep a tail pointer */
static void row_append(SeatRow *r, SeatNode *n) {
    if (!r->head) {
        r->head = r->tail = n;
    } else {
        r->tail->next = n;
        r->tail = n;
    }
    r->size++;
}

static void row_init(SeatRow *r, char label) {
    r->label = label;
    r->head = r->tail = NULL;
    r->size = 0;
    for (int i = 1; i <= SEATS_PER_ROW; i++) row_append(r, node_new(label, i));
}

static void row_free(SeatRow *r) {
    SeatNode *cur = r->head;
    while (cur) {
        SeatNode *next = cur->next;
        free(cur);
        cur = next;
    }
    r->head = r->tail = NULL;
    r->size = 0;
}

/* O(n): walk the list until the seat number matches */
static SeatNode *row_find(SeatRow *r, int seatNo) {
    for (SeatNode *cur = r->head; cur; cur = cur->next)
        if (cur->seatNo == seatNo) return cur;
    return NULL;
}

/* O(n): one pass that records every block of adjacent free seats */
static int row_free_runs(SeatRow *r, Run runs[], int max) {
    int n = 0, len = 0;
    SeatNode *start = NULL;
    for (SeatNode *cur = r->head; cur; cur = cur->next) {
        if (cur->bookingId == 0) {
            if (!start) start = cur;
            len++;
        } else if (start) {
            if (n < max) { runs[n].row = r; runs[n].start = start; runs[n].len = len; n++; }
            start = NULL;
            len = 0;
        }
    }
    if (start && n < max) { runs[n].row = r; runs[n].start = start; runs[n].len = len; n++; }
    return n;
}

/* Marks `count` linked seats starting at `start` with the booking id */
static int take_from(SeatNode *start, int count, int id) {
    int taken = 0;
    for (SeatNode *cur = start; cur && taken < count; cur = cur->next) {
        cur->bookingId = id;
        taken++;
    }
    return taken;
}

/* ---------- theatre ---------- */

void theatre_init(Theatre *t) {
    for (int i = 0; i < ROWS; i++) row_init(&t->rows[i], (char)('A' + i));
    t->bookings = NULL;
    t->nextId = 1;
}

void theatre_reset(Theatre *t) {
    for (int i = 0; i < ROWS; i++) row_free(&t->rows[i]);
    Booking *b = t->bookings;
    while (b) {
        Booking *next = b->next;
        free(b);
        b = next;
    }
    theatre_init(t);
}

/* "b7" -> node B7 (NULL if the label is not valid) */
SeatNode *theatre_find_seat(Theatre *t, const char *label) {
    if (!label || strlen(label) < 2) return NULL;
    char letter = (char)toupper((unsigned char)label[0]);
    if (letter < 'A' || letter >= 'A' + ROWS) return NULL;
    for (const char *p = label + 1; *p; p++)
        if (!isdigit((unsigned char)*p)) return NULL;
    long seatNo = strtol(label + 1, NULL, 10);
    if (seatNo < 1 || seatNo > SEATS_PER_ROW) return NULL;
    return row_find(&t->rows[letter - 'A'], (int)seatNo);
}

Booking *theatre_find_booking(Theatre *t, int id) {
    for (Booking *b = t->bookings; b; b = b->next)
        if (b->id == id) return b;
    return NULL;
}

int theatre_free_count(Theatre *t) {
    int free_seats = 0;
    for (int i = 0; i < ROWS; i++)
        for (SeatNode *cur = t->rows[i].head; cur; cur = cur->next)
            if (cur->bookingId == 0) free_seats++;
    return free_seats;
}

static int add_booking(Theatre *t, const char *name) {
    Booking *b = calloc(1, sizeof *b);
    if (!b) { perror("calloc"); exit(1); }
    b->id = t->nextId++;
    strncpy(b->name, name, NAME_LEN - 1);
    if (!t->bookings) {
        t->bookings = b;
    } else {
        Booking *tail = t->bookings;
        while (tail->next) tail = tail->next;
        tail->next = b;
    }
    return b->id;
}

static int fail_str(char *err, size_t errsz, const char *fmt, const char *arg) {
    snprintf(err, errsz, fmt, arg);
    return -1;
}

static int fail_num(char *err, size_t errsz, const char *fmt, int num) {
    snprintf(err, errsz, fmt, num);
    return -1;
}

/* Manual booking: check every seat first, then book them all (all or nothing) */
int theatre_book_seats(Theatre *t, const char *name, char labels[][LABEL_LEN],
                       int n, char *err, size_t errsz) {
    SeatNode *nodes[MAX_SEATS];
    if (n < 1 || n > MAX_SEATS) return fail_num(err, errsz, "Select between 1 and %d seats", MAX_SEATS);

    for (int i = 0; i < n; i++) {
        SeatNode *node = theatre_find_seat(t, labels[i]);
        if (!node) return fail_str(err, errsz, "Seat %s does not exist", labels[i]);
        if (node->bookingId != 0) return fail_str(err, errsz, "Seat %s is already booked", labels[i]);
        for (int j = 0; j < i; j++)
            if (nodes[j] == node) return fail_str(err, errsz, "Seat %s was selected twice", labels[i]);
        nodes[i] = node;
    }

    int id = add_booking(t, name);
    for (int i = 0; i < n; i++) nodes[i]->bookingId = id;
    return id;
}

/* Auto allocation, best fit: use the smallest block of adjacent free seats that
   still fits the group, so large blocks stay free for large groups.
   If no single block fits, split the group across the largest blocks. */
int theatre_auto_allocate(Theatre *t, const char *name, int count, int *split,
                          char *err, size_t errsz) {
    Run runs[SEATS_PER_ROW];
    Run best = {0};
    int found = 0;
    *split = 0;

    if (count < 1) return fail_num(err, errsz, "Group size must be at least %d", 1);

    for (int r = 0; r < ROWS; r++) {
        int n = row_free_runs(&t->rows[r], runs, SEATS_PER_ROW);
        for (int i = 0; i < n; i++) {
            if (runs[i].len >= count && (!found || runs[i].len < best.len)) {
                best = runs[i];
                found = 1;
            }
        }
    }

    if (found) {
        int id = add_booking(t, name);
        take_from(best.start, count, id);
        return id;
    }

    int free_seats = theatre_free_count(t);
    if (free_seats < count) return fail_num(err, errsz, "Only %d seats left", free_seats);

    int id = add_booking(t, name);
    int remaining = count;
    while (remaining > 0) {
        Run big = {0};
        int have = 0;
        for (int r = 0; r < ROWS; r++) {
            int n = row_free_runs(&t->rows[r], runs, SEATS_PER_ROW);
            for (int i = 0; i < n; i++)
                if (!have || runs[i].len > big.len) { big = runs[i]; have = 1; }
        }
        int want = remaining < big.len ? remaining : big.len;
        remaining -= take_from(big.start, want, id);
    }
    *split = 1;
    return id;
}

int theatre_cancel(Theatre *t, int id, char *err, size_t errsz) {
    Booking *prev = NULL, *cur = t->bookings;
    while (cur && cur->id != id) { prev = cur; cur = cur->next; }
    if (!cur) return fail_num(err, errsz, "Booking #%d not found", id);

    for (int r = 0; r < ROWS; r++)
        for (SeatNode *s = t->rows[r].head; s; s = s->next)
            if (s->bookingId == id) s->bookingId = 0;

    if (prev) prev->next = cur->next;
    else t->bookings = cur->next;
    free(cur);
    return id;
}
