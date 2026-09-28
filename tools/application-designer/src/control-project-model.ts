import type { MusicRaTControlProject } from './model'

export function createEmptyControlProject(): MusicRaTControlProject {
  return { schema_version: 1, devices: [], bindings: [], surfaces: [] }
}
