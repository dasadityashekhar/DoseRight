# DoseRight Backend

The DoseRight backend is a Node.js + Express + TypeScript API backed by MongoDB. It powers user authentication, medication plans, adherence tracking, and device (hardware) integration.

## Requirements

- Node.js 20+
- npm 9+
- MongoDB (local or hosted)

## Quick start

```bash
cd backend
npm install
```

Create `backend/.env` based on `backend/.env.example`:

```dotenv
PORT=8080
NODE_ENV=development
MONGODB_URI=mongodb://localhost:27017/doseright
JWT_SECRET=your-super-secret-jwt-key-change-this-in-production
DEVICE_SECRET=your-device-shared-secret-for-esp32
DEVICE_API_KEY=your-device-shared-secret-for-esp32
CORS_ORIGIN=http://localhost:5173
HARDWARE_TEST_MODE=false
```

Start the server:

```bash
npm run dev
```

API base URL: `http://localhost:8080/api`

## Scripts

```bash
npm run dev     # Run API in watch mode
npm run build   # Build to dist/
npm run start   # Run built server
npm run seed    # Seed demo data
npm run lint    # Lint TypeScript
npm run format  # Prettier format
```

## Authentication

### JWT (Dashboard API)

All dashboard routes require a Bearer token in `Authorization` header:

```
Authorization: Bearer <JWT_TOKEN>
```

Tokens are issued by `POST /api/auth/login` and `POST /api/auth/signup`.

### Device API authentication

Hardware endpoints require a device API key:

```
Authorization: Bearer <DEVICE_API_KEY>
```

Important: the device middleware currently checks `DEVICE_API_KEY`. Keep `DEVICE_API_KEY` in `.env`. `DEVICE_SECRET` is used elsewhere, so it is safe to set both to the same value.

## Data model overview

- **User**: name, email, phone, role (`patient`, `caretaker`, `doctor`, `admin`), passwordHash, isActive
- **Patient**: userId, deviceId, medicalProfile (illnesses, allergies, otherNotes), caretakers, doctors
- **Device**: deviceId, patientId, timezone, slotCount, status, heartbeat fields
- **MedicationPlan**: patientId, deviceId, slotIndex, name, strength, form, dosagePerIntake, times, daysOfWeek, stock
- **DoseLog**: patientId, deviceId, medicationPlanId, slotIndex, scheduledAt, status

## API reference

Base URL: `http://localhost:8080/api`

### Auth

#### POST /auth/signup
Create a new user.

Request body:

```json
{
  "name": "Alice Patient",
  "email": "alice@example.com",
  "phone": "9999999999",
  "password": "secret123",
  "role": "patient"
}
```

Response:

```json
{
  "message": "User created successfully",
  "user": {
    "id": "65f...",
    "name": "Alice Patient",
    "email": "alice@example.com",
    "phone": "9999999999",
    "role": "patient"
  },
  "token": "<JWT>"
}
```

#### POST /auth/login

Request body:

```json
{
  "email": "alice@example.com",
  "password": "secret123"
}
```

Response:

```json
{
  "message": "Login successful",
  "user": {
    "id": "65f...",
    "name": "Alice Patient",
    "email": "alice@example.com",
    "phone": "9999999999",
    "role": "patient"
  },
  "token": "<JWT>"
}
```

#### GET /auth/users
Debug-only user listing.

Response: array of users without `passwordHash`.

---

### Dashboard (JWT required)

#### GET /dashboard/medicines
Returns active medication plans for the logged-in patient.

#### POST /dashboard/medicines
Create a medication plan.

Request body (required fields: `medicationName`, `dosagePerIntake`, `times`, `daysOfWeek`, `slotIndex`):

```json
{
  "medicationName": "Metformin",
  "medicationStrength": "500mg",
  "medicationForm": "tablet",
  "dosagePerIntake": 1,
  "slotIndex": 1,
  "times": ["08:00", "20:00"],
  "daysOfWeek": [1, 2, 3, 4, 5, 6, 7],
  "startDate": "2026-02-14",
  "endDate": null,
  "stock": { "remaining": 30, "totalLoaded": 30 }
}
```

Response:

```json
{
  "message": "Medicine added successfully",
  "medicationPlan": { "_id": "65f...", "medicationName": "Metformin", "slotIndex": 1 }
}
```

#### PATCH /dashboard/medicines/:medicationId
Update a medication plan (partial updates supported).

Example body:

```json
{
  "dosagePerIntake": 2,
  "times": ["09:00"],
  "daysOfWeek": [1, 3, 5],
  "stockRemaining": 18,
  "active": true
}
```

#### GET /dashboard/schedule
Returns today’s schedule including synthetic pending items if no DoseLog exists.

#### GET /dashboard/adherence
Returns adherence summary.

Response:

```json
{
  "taken": 12,
  "missed": 3,
  "rate": 80,
  "takenPercent": 80
}
```

#### GET /dashboard/summary
Returns a quick dashboard summary:

```json
{ "activeMedicines": 2, "dosesTaken": 1, "dosesMissed": 0 }
```

#### GET /dashboard/history
Returns adherence history, weekly trend, and recent logs.

#### GET /dashboard/profile
Returns user, patient, and device profile data.

#### PATCH /dashboard/profile
Update user and patient medical profile.

Example body:

```json
{
  "name": "Alice Patient",
  "phone": "9999999999",
  "allergies": ["Penicillin"],
  "illnesses": ["Type 2 Diabetes"],
  "otherNotes": "Avoid NSAIDs"
}
```

#### POST /dashboard/device
Create or link a device to the logged-in patient.

```json
{ "deviceId": "DR-ESP32-001", "timezone": "Asia/Kolkata", "slotCount": 4 }
```

#### PATCH /dashboard/doses/:doseId/mark-taken
Mark a dose as taken. No body required.

#### PATCH /dashboard/doses/:doseId/mark-missed
Mark a dose as missed. No body required.

#### PATCH /dashboard/medications/:medicationId/refill
Refill medication stock.

```json
{ "amount": 30 }
```

---

### Hardware API (device key required)

#### GET /hardware/time
Optional query: `deviceId`. Returns server time and timezone information.

#### GET /hardware/profile
Query: `deviceId` (required). Returns device and patient profile.

#### GET /hardware/upcoming
Query: `deviceId` (required). Returns upcoming or recent pending doses.

Response:

```json
{
  "data": [
    {
      "doseId": "65f...",
      "medicineName": "Metformin",
      "dosage": "1 x 500mg",
      "scheduledTime": "08:00",
      "status": "pending",
      "slot": 1
    }
  ]
}
```

#### GET /hardware/taken
Query: `deviceId` (required). Doses taken in the last 7 days.

#### GET /hardware/missed
Query: `deviceId` (required). Doses missed in the last 7 days.

#### PATCH /hardware/doses/:doseId/mark-taken

```json
{ "deviceId": "DR-ESP32-001" }
```

#### PATCH /hardware/doses/:doseId/mark-skipped

```json
{ "deviceId": "DR-ESP32-001" }
```

#### POST /hardware/heartbeat

```json
{
  "deviceId": "DR-ESP32-001",
  "batteryLevel": 87,
  "wifiStrength": -50,
  "wifiConnected": true,
  "firmwareVersion": "1.0.0",
  "uptimeSeconds": 3600,
  "storageFreeKb": 12000,
  "temperatureC": 34,
  "lastError": null
}
```

Response: `{ "ok": true }`

---

### Device profile

#### GET /device/:deviceId/profile
Returns a read-only device + patient profile for hardware devices.

## Debug tools

- Hardware debug page: `GET /debug/hardware`
- Health check: `GET /health`

## Notes

- `CORS_ORIGIN` supports a comma-separated list for multiple frontends.
- If you use a hosted MongoDB, update `MONGODB_URI` accordingly.
- The server can serve the frontend build if `frontend/dist` exists.
