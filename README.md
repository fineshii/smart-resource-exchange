# Smart Resource Exchange

Smart Resource Exchange is a DAA based web application for student resource sharing. It models exchange and bidding flows for books, calculators, notes, and lab materials.

## What is included

- C++ backend in `server.cpp`
- SQLite database in `data/smart_resource_exchange.sqlite`
- Static frontend in `public/`
- Login and registration with per-user credit wallets
- Resource listing dashboard
- Student resource publishing form
- Offer and bidding form
- Semester credit grants and locked bidding credits
- Bot publisher accounts with starter marketplace listings
- Deadline-based allocation
- Priority score formula and greedy winner selection
- Internship and scholarship module

## DAA logic

```text
Priority Score = (Credit Score x 2) + Urgency Weight + Bid Value
```

When a resource deadline passes, the backend inserts all valid offers into a max priority queue and allocates the resource to the highest scoring offer.

## Run it

Build the C++ server:

```powershell
npm run build
```

Start the app:

```powershell
npm start
```

Then open [http://localhost:3000](http://localhost:3000).

## Storage

The backend stores app data in one SQLite database:

- `data/smart_resource_exchange.sqlite`

It creates and migrates these SQL tables automatically on startup:

- `resources`
- `offers`
- `users`
- `semesters`
- `credit_transactions`
- `internships`

New accounts, published resources, offers, semester grants, and credit movements are saved immediately and remain available after restarting the server. Demo bot accounts are also seeded with published resources so the marketplace is not empty.

## API

- `GET /api/resources`
- `POST /api/register`
- `POST /api/login`
- `POST /api/session`
- `POST /api/resources`
- `POST /api/offers`
