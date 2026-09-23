import { constants } from 'node:fs'
import { access, readFile, readdir } from 'node:fs/promises'
import { homedir } from 'node:os'
import { delimiter, join } from 'node:path'

const payloadDomains = new Map([
  ['CommRaT::Messages::AudioBlock', 'audio'],
  ['CommRaT::Messages::NoteEventBlock', 'note'],
  ['CommRaT::Messages::ParameterEventBlock', 'parameter'],
  ['CommRaT::Messages::DeckControlEventBlock', 'control'],
  ['CommRaT::Messages::TransportBlock', 'transport'],
  ['CommRaT::Messages::LevelMeterBlock', 'telemetry'],
  ['CommRaT::Messages::PlaybackStatusBlock', 'telemetry'],
])

const directionKeys = {
  output: 'outputs',
  input: 'inputs',
  synced_input: 'synced_inputs',
  remote: 'remotes',
}

const parameterKinds = new Set(['continuous', 'integer', 'boolean', 'choice', 'text'])
const parameterScales = new Set(['linear', 'logarithmic', 'decibel'])

function appendModuleDirectory(root) {
  return join(root, 'musicrat', 'modules')
}

export function descriptorSearchPaths(
  environment = process.env,
  fallbackDataDirectory = '/usr/local/share/musicrat/modules',
) {
  const paths = []
  if (environment.MUSICRAT_MODULE_PATH) {
    paths.push(...environment.MUSICRAT_MODULE_PATH.split(delimiter).filter(Boolean))
  }
  if (environment.XDG_DATA_HOME) {
    paths.push(appendModuleDirectory(environment.XDG_DATA_HOME))
  } else {
    paths.push(appendModuleDirectory(join(homedir(), '.local', 'share')))
  }
  const systemRoots = (environment.XDG_DATA_DIRS || '/usr/local/share:/usr/share')
    .split(delimiter)
    .filter(Boolean)
  paths.push(...systemRoots.map(appendModuleDirectory), fallbackDataDirectory)
  return [...new Set(paths)]
}

function validateDescriptor(descriptor, filename) {
  if (!descriptor || typeof descriptor !== 'object') throw new Error(`${filename}: descriptor must be an object`)
  if (typeof descriptor.module_class !== 'string' || !descriptor.module_class) {
    throw new Error(`${filename}: module_class is required`)
  }
  if (typeof descriptor.binary !== 'string' || !descriptor.binary) {
    throw new Error(`${filename}: binary is required`)
  }

  const ports = descriptor.descriptor_metadata?.musicrat_ports?.ports ?? []
  const ids = new Set()
  const positions = new Set()
  for (const port of ports) {
    const values = descriptor[directionKeys[port.direction]]
    if (!port.id || ids.has(port.id)) throw new Error(`${filename}: duplicate or empty port ID`)
    ids.add(port.id)
    if (!Array.isArray(values) || !Number.isInteger(port.port_index) || port.port_index < 0 || port.port_index >= values.length) {
      throw new Error(`${filename}: port ${port.id} has an invalid direction or index`)
    }
    const position = `${port.direction}:${port.port_index}`
    if (positions.has(position)) throw new Error(`${filename}: duplicate physical port position ${position}`)
    positions.add(position)
    const expectedDomain = payloadDomains.get(values[port.port_index])
    if (expectedDomain && expectedDomain !== port.domain) {
      throw new Error(`${filename}: port ${port.id} domain does not match its payload`)
    }
  }

  const parameters = descriptor.descriptor_metadata?.musicrat_parameters?.parameters ?? []
  const parameterIds = new Set()
  const parameterNames = new Set()
  for (const parameter of parameters) {
    if (!Number.isInteger(parameter.id) || parameter.id <= 0 || parameterIds.has(parameter.id)) {
      throw new Error(`${filename}: duplicate or invalid parameter ID`)
    }
    parameterIds.add(parameter.id)
    if (!parameter.name || parameterNames.has(parameter.name) || !(parameter.name in (descriptor.params_defaults ?? {}))) {
      throw new Error(`${filename}: duplicate or unknown parameter name`)
    }
    parameterNames.add(parameter.name)
    if (!parameterKinds.has(parameter.kind) || !parameterScales.has(parameter.display_scale)) {
      throw new Error(`${filename}: parameter ${parameter.name} has an unknown kind or display scale`)
    }
    if (['continuous', 'integer'].includes(parameter.kind)
      && (!Number.isFinite(parameter.minimum) || !Number.isFinite(parameter.maximum)
        || !Number.isFinite(parameter.step) || parameter.minimum >= parameter.maximum || parameter.step < 0)) {
      throw new Error(`${filename}: parameter ${parameter.name} has an invalid numeric range`)
    }
    if (parameter.kind === 'choice' && (!Array.isArray(parameter.choices) || parameter.choices.length === 0)) {
      throw new Error(`${filename}: choice parameter ${parameter.name} has no choices`)
    }
  }
  return descriptor
}

async function isExecutable(filename) {
  try {
    await access(filename, constants.X_OK)
    return true
  } catch {
    return false
  }
}

export async function discoverModuleDescriptors(options = {}) {
  const paths = options.paths ?? descriptorSearchPaths(options.environment, options.fallbackDataDirectory)
  const descriptors = []
  const warnings = []
  const classes = new Set()

  for (const directory of paths) {
    let entries
    try {
      entries = await readdir(directory, { withFileTypes: true })
    } catch (error) {
      if (error.code !== 'ENOENT') warnings.push(`${directory}: ${error.message}`)
      continue
    }
    for (const entry of entries.filter((item) => item.isFile() && item.name.endsWith('.module.json')).sort((a, b) => a.name.localeCompare(b.name))) {
      const filename = join(directory, entry.name)
      try {
        const descriptor = validateDescriptor(JSON.parse(await readFile(filename, 'utf8')), filename)
        if (classes.has(descriptor.module_class)) continue
        if (!await isExecutable(descriptor.binary)) {
          throw new Error(`${filename}: binary is missing or not executable: ${descriptor.binary}`)
        }
        classes.add(descriptor.module_class)
        descriptors.push(descriptor)
      } catch (error) {
        warnings.push(error instanceof Error ? error.message : `${filename}: invalid descriptor`)
      }
    }
  }

  return { descriptors, warnings, search_paths: paths }
}