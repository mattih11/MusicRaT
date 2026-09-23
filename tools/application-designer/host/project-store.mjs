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

const endpointDirections = new Set(['input', 'output', 'bidirectional'])
const endpointDomains = new Set([
  'unipolar', 'bipolar', 'relative', 'boolean', 'choice', 'gate', 'trigger',
  'note', 'transport', 'text', 'telemetry',
])
const bindingModes = new Set([
  'absolute', 'relative', 'toggle', 'momentary', 'gate', 'trigger', 'choice',
])
const surfaceTargets = new Set(['ratgui', 'lvgl', 'generic'])

function invalid(message) {
  throw new ProjectStoreError('invalid_document', message)
}

function record(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value)
}

function requireUniqueIds(items, description) {
  const ids = new Set()
  for (const item of items) {
    if (!record(item) || typeof item.id !== 'string' || !item.id.trim() || ids.has(item.id)) {
      invalid(`${description} IDs must be nonempty and unique.`)
    }
    ids.add(item.id)
  }
}

function validateEndpoint(endpoint) {
  if (typeof endpoint.display_name !== 'string' || !endpoint.display_name.trim()
      || !endpointDirections.has(endpoint.direction) || !endpointDomains.has(endpoint.domain)) {
    invalid(`Endpoint '${endpoint.id}' has invalid metadata.`)
  }
  for (const field of ['minimum', 'maximum', 'step']) {
    if (endpoint[field] !== undefined && !Number.isFinite(endpoint[field])) {
      invalid(`Endpoint '${endpoint.id}' ranges must contain finite numbers.`)
    }
  }
  if (endpoint.minimum !== undefined && endpoint.maximum !== undefined
      && endpoint.minimum > endpoint.maximum) {
    invalid(`Endpoint '${endpoint.id}' minimum cannot exceed its maximum.`)
  }
}

function validateControlProject(control, modules) {
  if (!record(control) || control.schema_version !== 1
      || !Array.isArray(control.devices) || !Array.isArray(control.bindings)
      || !Array.isArray(control.surfaces)) {
    invalid('musicrat_control must use schema_version 1 and contain device, binding, and surface arrays.')
  }

  const owners = [...control.devices, ...control.surfaces]
  requireUniqueIds(owners, 'Control owner')
  const endpoints = new Set()
  for (const owner of owners) {
    if (typeof owner.display_name !== 'string' || !owner.display_name.trim()
        || !Array.isArray(owner.endpoints)) {
      invalid(`Control owner '${owner.id}' has invalid metadata.`)
    }
    requireUniqueIds(owner.endpoints, `Endpoint in '${owner.id}'`)
    for (const endpoint of owner.endpoints) {
      validateEndpoint(endpoint)
      endpoints.add(`${owner.id}:${endpoint.id}`)
    }
  }

  const moduleIds = new Set(modules.map((module) => module.name))
  for (const device of control.devices) {
    if (!['hardware', 'gui', 'virtual'].includes(device.kind)) {
      invalid(`Device '${device.id}' has an invalid kind.`)
    }
    if (device.adapter_module_id !== undefined && !moduleIds.has(device.adapter_module_id)) {
      invalid(`Device '${device.id}' references a missing adapter module.`)
    }
  }

  requireUniqueIds(control.bindings, 'Binding')
  const bindingIds = new Set(control.bindings.map((binding) => binding.id))
  for (const binding of control.bindings) {
    const source = binding.source
    const target = binding.target
    if (!record(source) || !endpoints.has(`${source.owner_id}:${source.endpoint_id}`)) {
      invalid(`Binding '${binding.id}' references a missing source endpoint.`)
    }
    if (!record(target) || !moduleIds.has(target.module_id)
        || !Number.isInteger(target.parameter_id) || target.parameter_id <= 0) {
      invalid(`Binding '${binding.id}' has an invalid target parameter.`)
    }
    if (!bindingModes.has(binding.mode)) {
      invalid(`Binding '${binding.id}' has an invalid mode.`)
    }
    if (binding.pickup !== undefined && !['immediate', 'match'].includes(binding.pickup)) {
      invalid(`Binding '${binding.id}' has an invalid pickup policy.`)
    }
    if (binding.pickup_tolerance !== undefined
        && (!Number.isFinite(binding.pickup_tolerance) || binding.pickup_tolerance < 0)) {
      invalid(`Binding '${binding.id}' has an invalid pickup tolerance.`)
    }
    if (binding.feedback !== undefined
        && (!record(binding.feedback)
          || !endpoints.has(`${binding.feedback.owner_id}:${binding.feedback.endpoint_id}`))) {
      invalid(`Binding '${binding.id}' references a missing feedback endpoint.`)
    }
    if (binding.transform !== undefined) {
      if (!record(binding.transform)) invalid(`Binding '${binding.id}' has an invalid transform.`)
      for (const field of ['scale', 'offset', 'dead_zone', 'quantization', 'hysteresis']) {
        if (binding.transform[field] !== undefined && !Number.isFinite(binding.transform[field])) {
          invalid(`Binding '${binding.id}' transforms must contain finite numbers.`)
        }
      }
      if (binding.transform.curve !== undefined
          && !['linear', 'logarithmic', 'exponential'].includes(binding.transform.curve)) {
        invalid(`Binding '${binding.id}' has an invalid transform curve.`)
      }
    }
  }

  for (const surface of control.surfaces) {
    if (!surfaceTargets.has(surface.target) || !Array.isArray(surface.widgets)) {
      invalid(`Surface '${surface.id}' has invalid metadata.`)
    }
    requireUniqueIds(surface.widgets, `Widget in '${surface.id}'`)
    const surfaceEndpoints = new Set(surface.endpoints.map((endpoint) => endpoint.id))
    for (const widget of surface.widgets) {
      if (typeof widget.display_name !== 'string' || !widget.display_name.trim()
          || typeof widget.kind !== 'string' || !widget.kind.trim()) {
        invalid(`Widget '${widget.id}' has invalid metadata.`)
      }
      if (widget.endpoint_id !== undefined && !surfaceEndpoints.has(widget.endpoint_id)) {
        invalid(`Widget '${widget.id}' references a missing surface endpoint.`)
      }
      if (widget.binding_id !== undefined && !bindingIds.has(widget.binding_id)) {
        invalid(`Widget '${widget.id}' references a missing binding.`)
      }
    }
  }
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
  if (document.musicrat_control !== undefined) {
    validateControlProject(document.musicrat_control, document.modules)
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