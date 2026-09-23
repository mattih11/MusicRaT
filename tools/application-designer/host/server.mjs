import { createReadStream } from 'node:fs'
import { stat } from 'node:fs/promises'
import { createServer } from 'node:http'
import { homedir } from 'node:os'
import { extname, join, resolve, sep } from 'node:path'
import { fileURLToPath } from 'node:url'
import { discoverModuleDescriptors } from './catalog.mjs'
import { ProjectStore, ProjectStoreError } from './project-store.mjs'

const hostDirectory = fileURLToPath(new URL('.', import.meta.url))
const staticDirectory = resolve(hostDirectory, '..', 'dist')
const maxRequestBytes = 1024 * 1024

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

async function readJsonBody(request) {
  const chunks = []
  let size = 0
  for await (const chunk of request) {
    size += chunk.length
    if (size > maxRequestBytes) {
      throw new ProjectStoreError('too_large', 'Project request exceeds 1 MiB.')
    }
    chunks.push(chunk)
  }
  try {
    return JSON.parse(Buffer.concat(chunks).toString('utf8'))
  } catch {
    throw new ProjectStoreError('invalid_json', 'Request body must be valid JSON.')
  }
}

function projectNameFromPath(pathname) {
  const encodedName = pathname.slice('/api/projects/'.length)
  try {
    return decodeURIComponent(encodedName)
  } catch {
    throw new ProjectStoreError('invalid_name', 'Project name is not valid URL encoding.')
  }
}

function errorStatus(error) {
  if (!(error instanceof ProjectStoreError)) return 500
  if (error.code === 'not_found') return 404
  if (error.code === 'conflict') return 409
  if (error.code === 'too_large') return 413
  return 400
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

export function createDesignerServer({ modulePaths = [], projectStore } = {}) {
  const store = projectStore ?? new ProjectStore(defaultProjectDirectory())
  return createServer(async (request, response) => {
    try {
      const url = new URL(request.url ?? '/', `http://${request.headers.host ?? 'localhost'}`)
      if (request.method === 'GET' && url.pathname === '/api/catalog') {
        const catalog = await discoverModuleDescriptors(modulePaths.length ? { paths: modulePaths } : {})
        sendJson(response, 200, catalog)
        return
      }
      if (request.method === 'GET' && url.pathname === '/api/projects') {
        sendJson(response, 200, await store.list())
        return
      }
      if (url.pathname.startsWith('/api/projects/')) {
        const name = projectNameFromPath(url.pathname)
        if (request.method === 'GET') {
          sendJson(response, 200, await store.read(name))
          return
        }
        if (request.method === 'PUT') {
          const body = await readJsonBody(request)
          sendJson(response, 200, await store.write(
            name,
            body?.document,
            body?.expected_revision,
          ))
          return
        }
      }
      if (request.method === 'GET' && url.pathname === '/api/health') {
        sendJson(response, 200, { status: 'ok' })
        return
      }
      if (url.pathname.startsWith('/api/')) {
        sendJson(response, 404, { error: 'Not found' })
        return
      }
      if (request.method === 'GET' && await serveStatic(url.pathname, response)) return
      sendJson(response, 404, { error: 'Not found' })
    } catch (error) {
      sendJson(response, errorStatus(error), {
        error: error instanceof Error ? error.message : 'Internal server error.',
        code: error instanceof ProjectStoreError ? error.code : 'internal_error',
      })
    }
  })
}

function argumentValues(name) {
  return process.argv
    .filter((argument) => argument.startsWith(`--${name}=`))
    .map((argument) => argument.slice(name.length + 3))
    .filter(Boolean)
}

function defaultProjectDirectory(environment = process.env) {
  if (environment.MUSICRAT_PROJECT_DIR) return environment.MUSICRAT_PROJECT_DIR
  const configRoot = environment.XDG_CONFIG_HOME || join(homedir(), '.config')
  return join(configRoot, 'musicrat', 'applications')
}

const isMain = process.argv[1]
  && resolve(process.argv[1]) === fileURLToPath(import.meta.url)

if (isMain) {
  const port = Number(process.env.MUSICRAT_DESIGNER_PORT ?? 4174)
  const modulePaths = argumentValues('module-path')
    .flatMap((argument) => argument.split(':'))
  const [explicitProjectDirectory] = argumentValues('project-dir')
  const projectStore = new ProjectStore(
    explicitProjectDirectory ?? defaultProjectDirectory(),
  )
  createDesignerServer({ modulePaths, projectStore }).listen(port, '127.0.0.1', () => {
    console.log(`MusicRaT Application Designer: http://127.0.0.1:${port}`)
    console.log(`Project directory: ${projectStore.directory}`)
  })
}