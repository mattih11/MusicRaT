# MusicRaT Application Designer

Early browser-based editor for pre-launch MusicRaT applications. It consumes
generated CommRaT module descriptors, displays stable MusicRaT physical and
virtual ports, validates typed connections, and exports native CommRaT
application JSON.

This tool does not define a second graph format. Stable port IDs are editor-side
references resolved to descriptor positions during export.

## Development

Requires Node.js 18 or newer.

```bash
npm install
npm test
npm run dev
```

Run the local descriptor catalog in a second terminal:

```bash
MUSICRAT_MODULE_PATH=/path/to/modules npm run host
```

Vite proxies `/api/catalog` to the host on port 4174. The host searches
`MUSICRAT_MODULE_PATH`, the XDG user/system data directories, and the default
MusicRaT installation data directory in that order. A production build is also
served directly by the host at `http://127.0.0.1:4174`.

Without the host, the browser uses an explicit demonstration catalog. Project
files and descriptors can also be loaded manually. Process launching and atomic
project writes remain future host capabilities.
