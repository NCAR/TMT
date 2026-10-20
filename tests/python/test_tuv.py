import gc
import json
import os
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import numpy as np
import pytest
import musica

DEPS = Path(os.environ.get("TMT_TEST_DEPS", Path(__file__).resolve().parents[2] / "build/_deps"))
CONFIG = DEPS / "turbotuv/examples/tuv_5_4.json"


def model(config=None, grids=None, profiles=None, radiators=None):
    return musica.TUVX(grids if grids is not None else musica.GridMap(),
                       profiles if profiles is not None else musica.ProfileMap(),
                       radiators if radiators is not None else musica.RadiatorMap(),
                       config_string=json.dumps(config or json.loads(CONFIG.read_text())))


def test_dataset_and_independent_models():
    with model() as m:
        reference = m.run(30, 1.0)
        assert reference.actinic_flux.shape[-1] == 3
        assert list(reference.component.values) == ["direct", "upwelling", "downwelling"]
        assert len(m.photolysis_rate_names) == reference.sizes["reaction"]
        assert m.get_radiation_field_updater() is None
        second = m.run(110, 1.0)
        assert not np.array_equal(reference.photolysis_rate_constants, second.photolysis_rate_constants)
    def run(_):
        with model() as m:
            return m.run(30, 1.0)
    with ThreadPoolExecutor(2) as pool:
        for result in pool.map(run, range(2)):
            assert result.equals(reference)


def test_numpy_ownership_and_map_references():
    grid = musica.Grid(name="height", units="km", edges=[0, 1, 2])
    values = np.asarray(grid.edges)
    grid.close()
    gc.collect()
    values[1] = 0.8
    np.testing.assert_array_equal(values, [0, 0.8, 2])
    grids = musica.GridMap()
    grid = musica.Grid(name="height", units="km", edges=[0, 1, 2])
    grids.add_grid(grid)
    grid.close()
    grid = grids["height", "km"]
    grids.close()
    np.testing.assert_array_equal(grid.edges, [0, 1, 2])
    with pytest.raises(ValueError):
        grid.edges = [1, 2]


def test_host_profile_update_and_pointer_edits():
    config = json.loads(CONFIG.read_text())
    config["profiles"] = [p for p in config["profiles"] if p["name"] != "temperature"]
    with model() as original:
        height = original.get_grid_map()["height", "km"]
        host = musica.Profile(name="temperature", units="K", grid=height,
                              edge_values=220 + 60*np.exp(-height.edges/10),
                              midpoint_values=220 + 60*np.exp(-height.midpoints/10))
    profiles = musica.ProfileMap()
    profiles.add_profile(host)
    with model(config, profiles=profiles) as m:
        first = m.run(30, 1)
        host.edge_values[:] += 20
        host.midpoint_values[:] += 20
        second = m.run(30, 1)
        assert not np.array_equal(first.photolysis_rate_constants, second.photolysis_rate_constants)
        np.testing.assert_array_equal(m.get_profile_map()["temperature", "K"].edge_values, host.edge_values)


def test_host_radiation_and_lifetime():
    config = json.loads(CONFIG.read_text())
    config["radiative transfer"]["solver"] = {"type": "from host"}
    m = model(config)
    updater = m.get_radiation_field_updater()
    assert updater is not None
    nz, nw = m._shape[:2]
    values = np.ones((nw, nz))
    updater.update(values, values * 2, values * 3)
    result = m.run(30, 1.03)
    np.testing.assert_array_equal(result.actinic_flux[..., 0], values * 1.03)
    np.testing.assert_array_equal(result.actinic_flux[..., 1], values * 2.06)
    np.testing.assert_array_equal(result.actinic_flux[..., 2], values * 3.09)
    np.testing.assert_array_equal(result.spectral_irradiance, 0)
    with pytest.raises(ValueError):
        updater.update(values[:, :-1], values, values)
    m.close()
    updater.update(values, values, values)
    updater.close()


def test_errors_and_snapshot_ownership():
    with pytest.raises(ValueError):
        model({"invalid": True})
    m = model()
    grid = m.get_grid_map()["height", "km"]
    with pytest.raises(ValueError, match="read-only"):
        grid.edges = grid.edges.copy()
    m.close()
    assert grid.num_sections == 120
    with pytest.raises(KeyError):
        musica.GridMap()["missing", "km"]


@pytest.mark.parametrize('helper_name,configuration',[('v54','tuv_5_4'),('vTS1','ts1_tsmlt')])
def test_reference_helpers(helper_name,configuration):
    from importlib import import_module
    helper=import_module('musica.tuvx.'+helper_name)
    with helper.get_tuvx_calculator() as host, model(json.loads((DEPS/'turbotuv/examples'/f'{configuration}.json').read_text())) as configured:
        actual,reference=host.run(30,1.03),configured.run(30,1.03)
        # The reference helpers use their own tabulated-profile interpolation.
        # The full configuration uses TurboTUV's native interpolation path.
        for field in ('photolysis_rate_constants','heating_rates','dose_rates'):
            np.testing.assert_allclose(actual[field],reference[field],rtol=5e-6,atol=1e-20)
        assert np.isfinite(actual.actinic_flux).all()
