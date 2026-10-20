# Playhouse: Theatre Seat Allocation (Linked List in C)

A full stack web app where **everything on the backend is written in C**: the linked list, the seat allocation logic, and the HTTP server. The frontend is plain HTML, CSS and JavaScript.

**Live demo:** _add your deployed link here_
**Repository:** _add your GitHub link here_

## Problem statement
A theatre has rows of seats. Customers can pick specific seats, or give a group size and let the system find seats that sit side by side. Bookings can be cancelled and the seats become available again.

## Why a linked list?
Each row is a singly linked list: `head -> A1 -> A2 -> A3 -> ... -> NULL`.

```c
typedef struct SeatNode {
    char row;
    int seatNo;
    int bookingId;          /* 0 = free */
    struct SeatNode *next;
} SeatNode;
```

Finding adjacent free seats means walking the `next` pointers and counting consecutive free nodes, which is exactly what a linked list is good at. Bookings are also stored in a linked list (`Booking`), and cancelling one unlinks its node.

## Where the data structure is used
| Feature | Function | Time complexity |
|---|---|---|
| Build a row | `row_append` (tail pointer) | O(1) per seat |
| Find a seat | `row_find` | O(n) |
| Find blocks of adjacent free seats | `row_free_runs` | O(n) |
| Book chosen seats | `theatre_book_seats` | O(k x n) |
| Auto allocate a group | `theatre_auto_allocate` (best fit) | O(rows x n) |
| Cancel a booking | `theatre_cancel` (unlink + clear seats) | O(bookings + seats) |

n = seats in a row, k = seats chosen.

**Allocation strategy:** best fit. The system uses the smallest block of adjacent free seats that still fits the group, so big blocks stay available for big groups. If no single block fits, the group is split across the largest remaining blocks.

## Files
```
theatre.h / theatre.c   linked list + allocation logic (the DSA part)
server.c                small HTTP server and JSON API (sockets)
test.c                  tests for the linked list logic
public/                 frontend (HTML, CSS, JS)
Makefile, Dockerfile    build and deploy
```

## API
| Method | Route | Body |
|---|---|---|
| GET | `/api/theatre` | |
| POST | `/api/book/seats` | `{ "name": "Priya", "seats": ["A1","A2"] }` |
| POST | `/api/book/auto` | `{ "name": "Priya", "count": 3 }` |
| DELETE | `/api/booking/:id` | |
| POST | `/api/reset` | |

## Run locally
**Linux / macOS / WSL / Codespaces**
```bash
make test
make
./server
```

**Windows with MinGW gcc**
```bash
gcc -o server.exe server.c theatre.c -lws2_32
server.exe
```

Then open http://localhost:3000 (set the `PORT` environment variable to change it). Bookings are kept in memory and reset when the server restarts.

## Deploy on Render
1. Push this repo to GitHub (public).
2. On render.com choose New > Web Service and connect the repo.
3. Set **Language** to **Docker** (Render finds the `Dockerfile`), pick the free plan, and deploy.
4. Paste the URL at the top of this README.

## Screenshots
_Add screenshots of the seat map and the linked list view here._
