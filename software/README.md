# DoseRight

DoseRight is a full-stack medication adherence platform with a caregiver dashboard and a device-facing API for smart dispensers. It combines a React + Vite frontend with a Node.js + Express + MongoDB backend and includes hardware endpoints for ESP32-style devices.

## Features

- Role-based dashboards for patients, caretakers, doctors, and admins
- Medication plans, schedules, and adherence tracking
- Patient profiles and caretaker/doctor overviews
- Device API for time sync, upcoming doses, taken history, and heartbeat
- Built-in hardware debug page for quick API verification

## Tech stack

- Frontend: React, Vite, TypeScript, Tailwind CSS, React Query, Chart.js
- Backend: Node.js, Express, TypeScript, MongoDB (Mongoose), JWT, Zod

## Repository structure

```
backend/   # Express API, MongoDB models, auth, hardware endpoints
frontend/  # React UI (Vite + Tailwind)
README.md
```

## Prerequisites

- Node.js 20+
- npm 9+
- MongoDB running locally or a connection URI

## Quick start (local)

### 1) Backend setup

```bash
cd backend
npm install
```

Create a `.env` in `backend/` (use `.env.example` as a template):

```dotenv
PORT=8080
NODE_ENV=development
MONGODB_URI=mongodb://localhost:27017/doseright
JWT_SECRET=your-super-secret-jwt-key-change-this-in-production
DEVICE_SECRET=your-device-shared-secret-for-esp32
CORS_ORIGIN=http://localhost:5173
HARDWARE_TEST_MODE=false
```

Start the API:

```bash
npm run dev
```

The backend will run on `http://localhost:8080` and expose:

- Health check: `GET /health`
- API base: `http://localhost:8080/api`

### 2) Frontend setup

```bash
cd ../frontend
npm install
npm run dev
```

The frontend will run on `http://localhost:5173`.

## Backend scripts

From `backend/`:

```bash
npm run dev     # Run API in watch mode
npm run build   # Build to dist/
npm run start   # Run built server
npm run seed    # Seed demo data
npm run lint    # Lint TypeScript
npm run format  # Prettier format
```

## Frontend scripts

From `frontend/`:

```bash
npm run dev      # Start Vite dev server
npm run build    # Build for production
npm run preview  # Preview production build
```

## Environment variables

| Variable | Description | Example |
| --- | --- | --- |
| PORT | API port | `8080` |
| NODE_ENV | Runtime mode | `development` |
| MONGODB_URI | MongoDB connection string | `mongodb://localhost:27017/doseright` |
| JWT_SECRET | JWT signing secret | `replace-me` |
| DEVICE_SECRET | Shared secret for device API auth | `replace-me` |
| CORS_ORIGIN | Allowed frontend origins (comma-separated) | `http://localhost:5173` |
| HARDWARE_TEST_MODE | Mock device data for testing | `true` / `false` |

## API overview

Base URL: `http://localhost:8080/api`

### Auth

- `POST /auth/signup`
- `POST /auth/login`
- `GET /auth/users`

### Dashboard (JWT protected)

- `GET /dashboard/medicines`
- `POST /dashboard/medicines`
- `PATCH /dashboard/medicines/:medicationId`
- `GET /dashboard/schedule`
- `GET /dashboard/adherence`
- `GET /dashboard/summary`
- `GET /dashboard/history`
- `GET /dashboard/profile`
- `PATCH /dashboard/profile`
- `POST /dashboard/device`
- `GET /dashboard/caretaker/overview`
- `GET /dashboard/doctor/overview`
- `PATCH /dashboard/doses/:doseId/mark-taken`
- `PATCH /dashboard/doses/:doseId/mark-missed`
- `PATCH /dashboard/medications/:medicationId/refill`

### Device API (Bearer `DEVICE_SECRET`)

- `GET /hardware/time`
- `GET /hardware/profile`
- `GET /hardware/upcoming`
- `GET /hardware/taken`
- `GET /hardware/missed`
- `POST /hardware/heartbeat`
- `PATCH /hardware/doses/:doseId/mark-taken`
- `PATCH /hardware/doses/:doseId/mark-skipped`

### Device profile (Bearer `DEVICE_SECRET`)

- `GET /device/:deviceId/profile`

## Hardware debug page

Open `http://localhost:8080/debug/hardware` to run prebuilt hardware API requests and inspect responses.

## Production build

1) Build the frontend:

```bash
cd frontend
npm run build
```

2) Build and run the backend:

```bash
cd ../backend
npm run build
npm run start
```

If `frontend/dist` exists, the backend serves it automatically and supports SPA refresh.

## Notes

- Device API auth uses `Authorization: Bearer <DEVICE_SECRET>`.
- For multiple frontend origins, set `CORS_ORIGIN` as a comma-separated list.
