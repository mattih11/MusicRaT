import { createReadStream } from 'node:fs'
import { stat } from 'node:fs/promises'
import { createServer } from 'node:http'
import { extname, join, resolve, sep } from 'node:path'
import { fileURLToPath } from 'node:url'
import { discoverModuleDescriptors } from './catalog.mjs'

const hostDirectory = fileURLToPath(new URL('.', import.meta.url))
const staticDirectory = resolve(hostDirectory, '..', 'dist')
const port = Number(process.env.MUSICRAT_DESIGNER_PORT ?? 4174)
const explicitPaths = process.argv
  .filter((argument) => argument.startsWith('--module-path='))
  .flatMap((argument) => argument.slice('--module-path='.length).split(':'))
  .filter(Boolean)

const contentTypes = {
  '.css': 'text/css; charset=utf-8',
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.woff': 'font/woff',
  '.woff2': 'font/woff2',
}

function sendJson(response, status, value) {
  response.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Cache-Control': 'no-store',
  })
  response.end(JSON.stringify(value))
}

async function serveStatic(pathname, response) {
  const requested = pathname === '/' ? 'index.html' : pathname.slice(1)
  let filename = resolve(staticDirectory, requested)
  if (!filename.startsWith(`${staticDirectory}${sep}`) && filename !== staticDirectory) return false
  try {
    if (!(await stat(filename)).isFile()) return false
  } catch {
    filename = join(staticDirectory, 'index.html')
    try {
      if (!(await stat(filename)).isFile()) return false
    } catch {
      return false
    }
  }
  response.writeHead(200, { 'Content-Type': contentTypes[extname(filename)] ?? 'application/octet-stream' })
  createReadStream(filename).pipe(response)
  return true
}

const server = createServer(async (request, response) => {
  const url = new URL(request.url ?? '/', `http://${request.headers.host ?? 'localhost'}`)
  if (request.method === 'GET' && url.pathname === '/api/catalog') {
    const catalog = await discoverModuleDescriptors(explicitPaths.length ? { paths: explicitPaths } : {})
    sendJson(response, 200, catalog)
    return
  }
  if (request.method === 'GET' && url.pathname === '/api/health') {
    sendJson(response, 200, { status: 'ok' })
    return
  }
  if (request.method === 'GET' && await serveStatic(url.pathname, response)) return
  sendJson(response, 404, { error: 'Not found' })
})

server.listen(port, '127.0.0.1', () => {
  console.log(`MusicRaT Application Designer: http://127.0.0.1:${port}`)
})