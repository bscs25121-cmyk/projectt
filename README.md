# Zixel

A live, collaborative pixel map (inspired by wplace.live). Users enter a name, then paint pixels on a shared
pink world map. Backend: C++17, no external libraries. Frontend: plain HTML, CSS and JavaScript.

## Files

| File | What it is |
|---|---|
| `ds.hpp` | Hand-written Stack, Queue, MinHeap, Trie |
| `world.hpp`, `world.cpp` | Canvas, users, paint queue, undo, BFS region, leaderboard, search, save/load |
| `http.hpp`, `http.cpp` | Small multi-threaded HTTP server (Linux, macOS, Windows) |
| `main.cpp` | API routes and JSON helpers |
| `public/index.html`, `style.css`, `app.js` | The website (map, palette, login, image import) |
| `Dockerfile` | For deploying online |

## Build and run

Linux / macOS:
```
g++ -std=c++17 -O2 -pthread -o zixel main.cpp world.cpp http.cpp
./zixel
```
Windows (MinGW g++):
```
g++ -std=c++17 -O2 -o zixel.exe main.cpp world.cpp http.cpp -lws2_32
zixel.exe
```
Run it from this folder (it serves `public/`), then open http://localhost:8080 (set the `PORT` environment variable to change the port).

## Making it live (a link other people can open)

- Same Wi-Fi: open `http://<your-computer-ip>:8080` on another device.
- Public link from your own computer: run `ngrok http 8080` (or `cloudflared tunnel --url http://localhost:8080`).
- Hosted: push this folder to GitHub and deploy it on Render, Railway or Fly.io. They detect the `Dockerfile` and give you a URL. The canvas is saved to `zixel.save` (use a persistent disk if you want it to survive redeploys).

## How it works

- The world is 256 x 128 cells drawn over a simplified world map. The server stores only colours; the map outline is drawn by the browser.
- Browsers poll `/api/poll` every second, so everyone sees new pixels within about a second (live updates).
- Each user has 10 pixel "charges" that regenerate every 5 s. An image stamp (max 24 x 24) is allowed once per minute.
- Image import runs in the browser: resize, then map each pixel to the nearest palette colour (squared RGB distance). "Custom palette" lets you pick which colours the importer may use (select 2 or more).

## Data structures (for the report)

| Structure | Where | Use | Cost |
|---|---|---|---|
| Chunked 2D array in a hash map | `World::chunks` | Canvas stored as 32 x 32 chunks, created on demand | O(1) cell access |
| Hash map | `byTok`, `byName` | Session and name lookup | O(1) average |
| Queue (linked list) | `World::pending` | Paint requests applied in FIFO order | O(1) push/pop |
| Queue + BFS | `World::regionSize` | Size of the connected same-colour region | O(region size) |
| Stack | `User::undo` | Undo the last pixels you painted | O(1) pop |
| Min-heap | `World::top` | Top 5 painters using a heap of size k | O(n log k) |
| Trie | `World::trie` | Autocomplete for places and artwork titles | O(length of query) |
| Deque log | `World::log` | Version history; clients ask for changes since version N | O(changes) |

## API

| Route | Purpose |
|---|---|
| `POST /api/login` `{name}` | Enter with a name, returns a token |
| `GET /api/state` | Whole canvas, one character per cell |
| `GET /api/poll?since=V&token=T` | Changes since version V, charges, online count, leaderboard |
| `POST /api/paint` `{token,stamp,title,pixels:[[x,y,c]]}` | Paint pixels or a stamp |
| `POST /api/undo` `{token}` | Undo your last pixel |
| `GET /api/pixel?x=&y=` | Who painted a cell, when, region size |
| `GET /api/search?q=` | Trie search over places and artworks |

## Known limits

- Polling instead of WebSockets (simpler; a good "future work" item).
- Pixel owners and user accounts live in memory only; after a restart the colours are kept but owners and names are reset.
- No moderation beyond charges, the stamp cooldown and name filtering.
