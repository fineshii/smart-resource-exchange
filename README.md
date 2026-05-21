# Smart Resource Exchange

Smart Resource Exchange is a DAA based web application for student resource sharing. It models exchange and bidding flows for books, calculators, notes, and lab materials.

## What is included

- C++ backend in `server.cpp`
- File-backed database in `data/`
- Static frontend in `public/`
- Resource listing dashboard
- Offer and bidding form
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

The backend stores app data in three simple database files:

- `data/resources.db`
- `data/offers.db`
- `data/internships.db`

These files are created automatically on first run. New offers are saved immediately, and the server reloads them when it starts again.

## API

- `GET /api/resources`
- `POST /api/offers`
