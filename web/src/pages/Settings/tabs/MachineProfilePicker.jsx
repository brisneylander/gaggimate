import { useEffect, useRef, useState } from 'preact/hooks';
import { Trans, useLingui } from '@lingui/react/macro';
import {
  BUILT_IN_PROFILES,
  profileFromForm,
  profileHash,
  profileInUse,
  validateProfile,
} from './machineProfiles.js';

// Pick a built-in machine profile, or import / export one as JSON. Applying fills the form; Save stores it.
export function MachineProfilePicker({ formData, setField }) {
  const { t } = useLingui();
  // The profile the settings are (a built-in they match, or the last one applied if unchanged): shown in use, and a
  // built-in is preselected once the settings have loaded.
  const inUse = profileInUse(formData);
  const [selected, setSelected] = useState('');
  const [note, setNote] = useState('');
  const fileInput = useRef(null);
  useEffect(() => {
    if (inUse.name && selected === '' && BUILT_IN_PROFILES.some(p => p.name === inUse.name))
      setSelected(inUse.name);
  }, [inUse.name]);

  const apply = (values, label) => {
    for (const [key, value] of Object.entries(values)) setField(key, value);
    // Remember which profile this is (saved with the settings), so the page can name it later.
    setField('machineProfileName', label);
    setField('machineProfileHash', profileHash({ ...formData, ...values }));
    setNote(t`${label} applied to the machine profile: review, then Save.`);
  };

  const onApply = () => {
    const profile = BUILT_IN_PROFILES.find(p => p.name === selected);
    if (profile) apply(profile.values, profile.name);
  };

  const onExport = () => {
    const name = window.prompt(
      t`Name for this machine profile`,
      inUse.name || formData.machineProfileName || t`My machine`,
    );
    if (!name) return;
    const blob = new Blob([JSON.stringify(profileFromForm(name, formData), null, 2)], {
      type: 'application/json',
    });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `${name.replace(/[^a-z0-9_-]+/gi, '-').toLowerCase()}.machine-profile.json`;
    a.click();
    URL.revokeObjectURL(url);
  };

  const onImportFile = e => {
    const file = e.target.files?.[0];
    e.target.value = '';
    if (!file) return;
    const reader = new FileReader();
    reader.onload = () => {
      let parsed;
      try {
        parsed = JSON.parse(reader.result);
      } catch {
        setNote(t`Not a valid JSON file.`);
        return;
      }
      const { values, errors } = validateProfile(parsed);
      if (Object.keys(values).length === 0) {
        setNote(t`Nothing imported: ${errors.join('; ') || 'no settings in the file'}.`);
        return;
      }
      apply(values, parsed.name || file.name);
      if (errors.length)
        setNote(t`Imported with warnings: ${errors.join('; ')}. Review, then Save.`);
    };
    reader.readAsText(file);
  };

  return (
    <div className='mb-4 flex flex-col gap-2'>
      <div className='flex flex-wrap items-center gap-2'>
        <label htmlFor='machineProfile' className='text-sm font-medium'>
          <Trans>Machine profile</Trans>
        </label>
        <select
          id='machineProfile'
          className='select select-bordered select-sm'
          value={selected}
          onChange={e => setSelected(e.target.value)}
        >
          <option value=''>{t`Choose…`}</option>
          {BUILT_IN_PROFILES.map(p => (
            <option key={p.name} value={p.name}>
              {p.name}
            </option>
          ))}
        </select>
        <button type='button' className='btn btn-sm' disabled={!selected} onClick={onApply}>
          <Trans>Apply</Trans>
        </button>
        <button
          type='button'
          className='btn btn-sm btn-outline'
          onClick={() => fileInput.current?.click()}
        >
          <Trans>Import JSON</Trans>
        </button>
        <button type='button' className='btn btn-sm btn-outline' onClick={onExport}>
          <Trans>Export JSON</Trans>
        </button>
        <input
          ref={fileInput}
          type='file'
          accept='application/json,.json'
          className='hidden'
          onChange={onImportFile}
        />
      </div>
      <p className='text-sm opacity-70'>
        <Trans>
          Fills in the machine-specific values below (boiler model, delays, PID gains, pump
          calibration, flush to target, tank warming, safety times, temperature offset). Tank
          temperatures are left as they are.
        </Trans>
      </p>
      <p className='text-sm'>
        {inUse.name ? (
          <Trans>In use: {inUse.name}</Trans>
        ) : inUse.changedFrom ? (
          <Trans>In use: custom values (changed from {inUse.changedFrom})</Trans>
        ) : (
          <Trans>In use: custom values</Trans>
        )}
      </p>
      {note && <p className='text-sm'>{note}</p>}
    </div>
  );
}
