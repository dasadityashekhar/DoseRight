# DoseRight Frontend

The DoseRight frontend is a React + Vite + TypeScript UI styled with Tailwind CSS. It includes dashboards for patients, caretakers, doctors, and admins and connects to the backend API via React Query.

## Requirements

- Node.js 20+
- npm 9+

## Quick start

```bash
cd frontend
npm install
npm run dev
```

The dev server runs on `http://localhost:5173`.

## Environment variables

Create `frontend/.env` (optional) to point to a custom API URL:

```dotenv
VITE_API_URL=http://localhost:8080
```

If not set, the app defaults to `http://localhost:8080`.

## Scripts

```bash
npm run dev      # Start Vite dev server
npm run build    # Build for production
npm run preview  # Preview production build
```

## Project structure

```
src/
  components/    # Reusable UI components
  hooks/         # API hooks (React Query + Axios)
  lib/           # Auth helpers
  pages/         # Dashboards and auth page
  App.tsx        # App shell and routing logic
  main.tsx       # App entrypoint
```

## API usage

The frontend calls the backend through `src/hooks/useApi.ts`. All dashboard endpoints require a JWT in the `Authorization` header. The app stores the token in memory (login flow) and passes it to each hook.

Example API base resolution:

- `VITE_API_URL` or `VITE_API_BASE_URL` if set
- otherwise defaults to `http://localhost:8080`

## Pages

- `AuthPage` (login/signup)
- `PatientDashboard`
- `CaretakerDashboard`
- `DoctorDashboard`
- `AdminDashboard`
- `PatientDetails`

## Build and deploy

```bash
npm run build
```

This generates `dist/`. If you build the frontend and run the backend from `backend/`, the backend will serve `frontend/dist` automatically when it exists.

## Notes

- The UI uses React Query caching with sensible defaults for refresh and stale times.
- Charts are rendered with Chart.js via `react-chartjs-2`.
