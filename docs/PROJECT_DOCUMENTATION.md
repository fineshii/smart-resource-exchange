# Smart Resource Exchange System

## Overview

Smart Resource Exchange is a DAA based web application for students to share, exchange, or bid for academic resources such as books, calculators, notes, and lab materials.

The core purpose is to demonstrate real algorithmic decision-making instead of first-come-first-serve allocation. The backend calculates offer priority, stores submitted offers, and allocates expired listings using a priority queue and greedy selection.

## Modules

### Resource Management

Students can view academic resources with owner, type, mode, urgency, deadline, offer count, and allocation status.

### Offer and Bidding

Students can submit exchange requests or bids before the listing deadline. Each offer stores the student name, credit score, urgency level, selected mode, bid value, calculated priority score, and timestamp.

### Time-Bound Scheduling

The backend checks deadlines whenever resource data is requested or offers are submitted. Late offers are rejected, and expired resources are allocated automatically.

### DAA Optimization

Each offer receives a priority score:

```text
Priority Score = (Credit Score x 2) + Urgency Weight + Bid Value
```

Urgency weights:

- High: 30
- Medium: 18
- Low: 8

For exchange mode, bid value is ignored. For bidding mode, bid value contributes to the final score.

### Greedy Allocation

When a deadline passes, valid offers are inserted into a max priority queue. The highest-priority offer is extracted and selected as the winner.

Time complexity:

```text
O(n log n)
```

### Storage

The C++ backend uses a small file-backed database in `data/`:

- `resources.db`
- `offers.db`
- `internships.db`

These files are created automatically at runtime and are ignored by Git so local data does not leak into the repository.

## API

### GET `/api/resources`

Returns all resources and internship/scholarship records.

### POST `/api/offers`

Submits an offer for a resource.

Example request:

```json
{
  "resourceId": 1,
  "studentName": "Priya",
  "credits": 27,
  "urgency": "High",
  "mode": "Bidding",
  "bidValue": 60
}
```

Example response:

```json
{
  "ok": true,
  "score": 144
}
```

## Technology Stack

- Frontend: HTML, CSS, JavaScript
- Backend: C++ with Winsock HTTP server
- Storage: Local file-backed database
- Algorithm: Priority queue with greedy selection
