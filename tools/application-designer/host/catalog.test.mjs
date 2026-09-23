import { chmod, mkdir, mkdtemp, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { afterEach, describe, expect, it } from 'vitest'
import { descriptorSearchPaths, discoverModuleDescriptors } from './catalog.mjs'

const temporaryDirectories = []

afterEach(async () => {
  await Promise.all(temporaryDirectories.splice(0).map((directory) => rm(directory, { recursive: true, force: true })))
})

async function fixture(moduleClass, domain = 'note', parameterKind) {
  const root = await mkdtemp(join(tmpdir(), 'musicrat-catalog-'))
  temporaryDirectories.push(root)
  const binary = join(root, moduleClass)
  await writeFile(binary, '#!/bin/sh\n')
  await chmod(binary, 0o755)
  const descriptor = {
    module_class: moduleClass,
    binary,
    outputs: ['CommRaT::Messages::NoteEventBlock'],
    inputs: [], synced_inputs: [], remotes: [],
    params_defaults: parameterKind ? { path: 'input.wav' } : undefined,
    descriptor_metadata: {
      musicrat_ports: { schema_version: 1, ports: [
        { id: 'notes', display_name: 'Notes', direction: 'output', port_index: 0, domain, required: true, max_connections: 1 },
      ] },
      musicrat_parameters: parameterKind ? { schema_version: 1, parameters: [
        { id: 1, name: 'path', display_name: 'Path', group: 'Media', kind: parameterKind, unit: '', minimum: 0, maximum: 1, step: 0, display_scale: 'linear', automatable: false, read_only: false, choices: [] },
      ] } : undefined,
    },
  }
  await writeFile(join(root, `${moduleClass}.module.json`), JSON.stringify(descriptor))
  return root
}

describe('installed descriptor discovery', () => {
  it('uses explicit paths before XDG locations', () => {
    const paths = descriptorSearchPaths({ MUSICRAT_MODULE_PATH: '/dev/one:/dev/two', XDG_DATA_HOME: '/home/data', XDG_DATA_DIRS: '/opt/share' })
    expect(paths.slice(0, 4)).toEqual(['/dev/one', '/dev/two', '/home/data/musicrat/modules', '/opt/share/musicrat/modules'])
  })

  it('loads a valid descriptor whose binary is executable', async () => {
    const root = await fixture('MusicRaTNotes')
    const result = await discoverModuleDescriptors({ paths: [root] })
    expect(result.descriptors.map((item) => item.module_class)).toEqual(['MusicRaTNotes'])
    expect(result.warnings).toEqual([])
  })

  it('rejects payload-domain mismatches', async () => {
    const root = await fixture('MusicRaTInvalid', 'audio')
    const result = await discoverModuleDescriptors({ paths: [root] })
    expect(result.descriptors).toEqual([])
    expect(result.warnings[0]).toContain('domain does not match its payload')
  })

  it('accepts text parameters and rejects unknown kinds', async () => {
    const validRoot = await fixture('MusicRaTText', 'note', 'text')
    const invalidRoot = await fixture('MusicRaTUnknown', 'note', 'path')
    const result = await discoverModuleDescriptors({ paths: [validRoot, invalidRoot] })
    expect(result.descriptors.map((item) => item.module_class)).toEqual(['MusicRaTText'])
    expect(result.warnings[0]).toContain('unknown kind')
  })
})