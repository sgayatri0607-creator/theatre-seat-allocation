#ifndef THEATRE_H
#define THEATRE_H

#include <stddef.h>

#define ROWS 8
#define SEATS_PER_ROW 12
#define MAX_SEATS (ROWS * SEATS_PER_ROW)
#define LABEL_LEN 16
#define NAME_LEN 41

/* One seat = one node of a singly linked list. bookingId 0 means free. */
typedef struct SeatNode {
    char row;
    int seatNo;
    int bookingId;
    struct SeatNode *next;
} SeatNode;

/* One row = a linked list: head -> A1 -> A2 -> ... -> NULL */
typedef struct {
    char label;
    SeatNode *head;
    SeatNode *tail;
    int size;
} SeatRow;

/* Bookings are also kept in a linked list. */
typedef struct Booking {
    int id;
    char name[NAME_LEN];
    struct Booking *next;
} Booking;

typedef struct {
    SeatRow rows[ROWS];
    Booking *bookings;
    int nextId;
} Theatre;

/* A block of adjacent free seats inside one row. */
typedef struct {
    SeatRow *row;
    SeatNode *start;
    int len;
} Run;

void theatre_init(Theatre *t);
void theatre_reset(Theatre *t);

SeatNode *theatre_find_seat(Theatre *t, const char *label);
Booking *theatre_find_booking(Theatre *t, int id);
int theatre_free_count(Theatre *t);

/* The three functions below return the booking id, or -1 and fill err. */
int theatre_book_seats(Theatre *t, const char *name, char labels[][LABEL_LEN],
                       int n, char *err, size_t errsz);
int theatre_auto_allocate(Theatre *t, const char *name, int count, int *split,
                          char *err, size_t errsz);
int theatre_cancel(Theatre *t, int id, char *err, size_t errsz);

#endif
