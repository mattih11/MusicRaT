import { createHash, randomUUID } from 'node:crypto'
import { open, mkdir, readFile, readdir, rename, rm } from 'node:fs/promises'
import { resolve } from 'node:path'

const projectNamePattern = /^[A-Za-z0-9][A-Za-z0-9._ -]{0,126}\.json$/

export class ProjectStoreError extends Error {
  constructor(code, message) {
    super(message)
    this.code = code
  }
}

function revisionOf(content) {
  return createHash('sha256').update(content).digest('hex')
}

function validateDocument(document) {
  if (!document || typeof document !== 'object' || Array.isArray(document)) {
    throw new ProjectStoreError('invalid_document', 'Project must be a JSON object.')
  }
  if (typeof document.app_name !== 'string' || !document.app_name.trim()) {
    throw new ProjectStoreError('invalid_document', 'Project app_name must be a nonempty string.')
  }
  if (!Array.isArray(document.modules)) {
    throw new ProjectStoreError('invalid_document', 'Project modules must be an array.')
  }
}

async function syncDirectory(directory) {
  const handle = await open(directory, 'r')
  try {
    await handle.sync()
  } finally {
    await handle.close()
  }
}

export class ProjectStore {
  constructor(directory) {
    this.directory = resolve(directory)
    this.pendingWrites = new Map()
  }

  filename(name) {
    if (typeof name !== 'string' || !projectNamePattern.test(name)) {
      throw new ProjectStoreError(
        'invalid_name',
        'Project names must end in .json and contain only letters, numbers, spaces, dots, underscores, or hyphens.',
      )
    }
    return resolve(this.directory, name)
  }

  async read(name) {
    const filename = this.filename(name)
    let content
    try {
      content = await readFile(filename, 'utf8')
    } catch (error) {
      if (error?.code === 'ENOENT') {
        throw new ProjectStoreError('not_found', `Project '${name}' does not exist.`)
      }
      throw error
    }

    let document
    try {
      document = JSON.parse(content)
    } catch {
      throw new ProjectStoreError('invalid_document', `Project '${name}' is not valid JSON.`)
    }
    validateDocument(document)
    return { name, revision: revisionOf(content), document }
  }

  async list() {
    await mkdir(this.directory, { recursive: true })
    const entries = await readdir(this.directory, { withFileTypes: true })
    const projects = []
    const warnings = []
    for (const entry of entries
      .filter((item) => item.isFile() && projectNamePattern.test(item.name))
      .sort((left, right) => left.name.localeCompare(right.name))) {
      try {
        const project = await this.read(entry.name)
        projects.push({
          name: project.name,
          revision: project.revision,
          app_name: project.document.app_name,
          module_count: project.document.modules.length,
        })
      } catch (error) {
        warnings.push(error instanceof Error ? error.message : String(error))
      }
    }
    return { projects, warnings }
  }

  write(name, document, expectedRevision) {
    const previous = this.pendingWrites.get(name) ?? Promise.resolve()
    const operation = previous
      .catch(() => {})
      .then(() => this.writeExclusive(name, document, expectedRevision))
    this.pendingWrites.set(name, operation)
    return operation.finally(() => {
      if (this.pendingWrites.get(name) === operation) this.pendingWrites.delete(name)
    })
  }

  async writeExclusive(name, document, expectedRevision) {
    const filename = this.filename(name)
    if (expectedRevision !== null && typeof expectedRevision !== 'string') {
      throw new ProjectStoreError(
        'invalid_revision',
        'expected_revision must be the last loaded revision or null for a new project.',
      )
    }
    validateDocument(document)
    await mkdir(this.directory, { recursive: true })

    let currentRevision = null
    try {
      currentRevision = revisionOf(await readFile(filename))
    } catch (error) {
      if (error?.code !== 'ENOENT') throw error
    }
    if (currentRevision !== expectedRevision) {
      throw new ProjectStoreError(
        'conflict',
        currentRevision === null
          ? `Project '${name}' was removed after it was loaded.`
          : `Project '${name}' has changed; reopen it before saving.`,
      )
    }

    const content = `${JSON.stringify(document, null, 2)}\n`
    const temporary = resolve(this.directory, `.${name}.${process.pid}.${randomUUID()}.tmp`)
    let handle
    try {
      handle = await open(temporary, 'wx', 0o600)
      await handle.writeFile(content, 'utf8')
      await handle.sync()
      await handle.close()
      handle = undefined
      await rename(temporary, filename)
      await syncDirectory(this.directory)
    } catch (error) {
      await handle?.close().catch(() => {})
      await rm(temporary, { force: true }).catch(() => {})
      throw error
    }

    return { name, revision: revisionOf(content), document }
  }
}