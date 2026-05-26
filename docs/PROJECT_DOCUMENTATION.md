# Smart Resource Exchange System

## Overview

Smart Resource Exchange is a DAA based web application for students to share or bid for academic resources such as books, calculators, notes, and lab materials.

The core purpose is to demonstrate real algorithmic decision-making instead of first-come-first-serve allocation. The backend calculates offer priority, stores submitted offers in SQLite, allows students to publish their own listings, and allocates expired listings using a priority queue and greedy selection.

The current version also uses student accounts. Each user has a semester credit wallet, bidding can lock credits until allocation, selected sellers receive the winning bid credits after their listing is allocated, and High urgency can only be used twice per semester.

## Modules

### Resource Management

Students can view academic resources with owner, type, mode, urgency, deadline, offer count, and allocation status.

Students can also publish their own resources by entering the title, owner name, type, mode, urgency, deadline window, and listing details.

Seeded bot publisher accounts publish starter resources such as library books, lab materials, notes, and placement workbooks. These records use the same `resources` table as student-published listings, with publish metadata attached.

The listing window also shows the logged-in user's published resources. Once a resource is allocated, the owner can see the winner name and account email for contact.

### Offer and Bidding

Students can submit sharing requests or credit bids before the listing deadline. Each offer stores the student name, credit score, urgency level, selected mode, bid value, calculated priority score, and timestamp.

### Time-Bound Scheduling

The backend checks deadlines whenever resource data is requested or offers are submitted. Late offers are rejected, and expired resources are allocated automatically.

### DAA Optimization

Each offer receives a priority score:

```text
Priority Score = (Base Credit Score x 2) + Urgency Weight + Bid Value
```

The base credit score is capped at 50 from the user's currently available wallet credits.

Urgency weights:

- High: 30
- Medium: 18
- Low: 8

For sharing mode, bid value is ignored. For bidding mode, bid value contributes to the final score.

High urgency is intentionally limited to two submitted offers per student per semester so users cannot mark every request as highest priority.

### Greedy Allocation

When a deadline passes, valid offers are inserted into a max priority queue. The highest-priority offer is extracted and selected as the winner.

Tie breakers are deterministic:

- Higher priority score wins.
- If scores match, the earlier offer wins.
- If score and timestamp both match, the lower user ID wins.

Time complexity:

```text
O(n log n)
```

### Storage

The C++ backend uses a SQLite database in `data/`:

- `smart_resource_exchange.sqlite`

The database is created automatically at runtime and is ignored by Git so local data does not leak into the repository.

Tables:

- `resources`
- `offers`
- `users`
- `semesters`
- `credit_transactions`
- `internships`

Important fields:

- `users.role` and `users.is_bot` mark student accounts versus seeded bot publisher accounts.
- `resources.status` and `resources.published_at` track publication state and publish time.
- `offers.locked_credits` and `offers.status` track active bidding commitments.
- `credit_transactions.reason` records semester grants, locked bid releases, and seller credit transfers.

## API

### GET `/api/resources`

Returns all resources, winner contact fields for allocated resources, public user wallet summaries, active semester data, and internship/scholarship records.

### POST `/api/register`

Creates a student account and grants the active semester credits.

### POST `/api/login`

Logs in an existing account and returns an auth token for protected actions.

### POST `/api/session`

Refreshes the current user session from an auth token.

### POST `/api/offers`

Submits an offer for a resource.

Example request:

```json
{
  "resourceId": 1,
  "authToken": "student-session-token",
  "urgency": "High",
  "mode": "Bidding",
  "bidValue": 60
}
```

Example response:

```json
{
  "ok": true,
  "score": 190,
  "availableCredits": 40
}
```

### POST `/api/resources`

Publishes a new student-owned resource listing.

Example request:

```json
{
  "title": "Engineering Drawing Kit",
  "type": "Lab Material",
  "authToken": "student-session-token",
  "description": "Available for first-year graphics practicals.",
  "urgency": "Medium",
  "mode": "Sharing",
  "durationMinutes": 1440
}
```

## Technology Stack

- Frontend: HTML, CSS, JavaScript
- Backend: C++ with Winsock HTTP server
- Storage: SQLite
- Algorithm: Priority queue with greedy selection
