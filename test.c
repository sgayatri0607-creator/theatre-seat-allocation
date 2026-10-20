#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "theatre.h"

static int seat_is(Theatre *t, const char *label, int id) {
    SeatNode *n = theatre_find_seat(t, label);
    return n && n->bookingId == id;
}

int main(void) {
    Theatre t;
    char err[128];
    int split;
    theatre_init(&t);

    /* auto allocation seats a group side by side */
    int a = theatre_auto_allocate(&t, "Asha", 3, &split, err, sizeof err);
    assert(a > 0 && !split);
    assert(seat_is(&t, "A1", a) && seat_is(&t, "A2", a) && seat_is(&t, "A3", a));

    /* best fit: the 9-seat gap in row A is used before an empty row */
    int b = theatre_auto_allocate(&t, "Ben", 4, &split, err, sizeof err);
    assert(seat_is(&t, "A4", b) && seat_is(&t, "A7", b));

    /* double booking is rejected */
    char labels[1][LABEL_LEN] = {"A1"};
    assert(theatre_book_seats(&t, "Cy", labels, 1, err, sizeof err) == -1);
    assert(strstr(err, "already booked"));

    /* bad seat is rejected */
    strcpy(labels[0], "Z9");
    assert(theatre_book_seats(&t, "Cy", labels, 1, err, sizeof err) == -1);

    /* cancel frees the seats again */
    assert(theatre_cancel(&t, a, err, sizeof err) == a);
    assert(seat_is(&t, "A2", 0));
    assert(theatre_free_count(&t) == MAX_SEATS - 4);
    assert(theatre_cancel(&t, a, err, sizeof err) == -1);

    /* splitting when no single block fits */
    theatre_reset(&t);
    for (int r = 0; r < ROWS; r++) {
        char two[2][LABEL_LEN];
        snprintf(two[0], LABEL_LEN, "%c4", 'A' + r);
        snprintf(two[1], LABEL_LEN, "%c10", 'A' + r);
        assert(theatre_book_seats(&t, "X", two, 2, err, sizeof err) > 0);
    }
    /* every row now has runs of 3, 5 and 3 free seats; ask for 6 */
    int g = theatre_auto_allocate(&t, "Group", 6, &split, err, sizeof err);
    assert(g > 0 && split == 1);

    /* not enough seats */
    theatre_reset(&t);
    assert(theatre_auto_allocate(&t, "Big", MAX_SEATS + 1, &split, err, sizeof err) == -1);

    theatre_reset(&t);
    printf("All tests passed\n");
    return 0;
}
