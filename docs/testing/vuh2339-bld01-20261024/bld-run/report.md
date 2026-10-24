# net_twilight_thorn_bots (attempt 1): FAIL

Started 2026-10-23T23:44:45, 627.4 s, instances [15284, 9100].

Automatic desync collection: **PARTIAL** (separate from scenario step status).

- [desync-reports/88d9fe35e873bbecb786add21881523d/1/manifest.json](desync-reports/88d9fe35e873bbecb786add21881523d/1/manifest.json): partial
- [desync-reports/88d9fe35e873bbecb786add21881523d/2/manifest.json](desync-reports/88d9fe35e873bbecb786add21881523d/2/manifest.json): partial

**Error:** step 42 (assert): An owned combat pilot failed; the current paired clip block is finalized. See host_thorn/friend_thorn pilot logs. Stop before recording another block.

| # | step | status | s | detail |
|---|---|---|---|---|
| 0 | boot | pass | 47.47 | {"processId": 15284, "instance": 0, "world": 2, "room": 32} |
| 1 | boot | pass | 46.73 | {"processId": 9100, "instance": 1, "world": 2, "room": 32} |
| 2 | assert | pass | 0.02 | {"value": "True"} |
| 3 | assert | pass | 0.03 | {"value": "True"} |
| 4 | assert | pass | 0.0 | {"value": "True"} |
| 5 | relay | pass | 1.03 | {"pid": 9540, "port": "27809"} |
| 6 | runtime | pass | 0.53 | {"pid": 22224, "role": "player", "link": {}} |
| 7 | runtime | pass | 0.52 | {"pid": 12216, "role": "friend1", "link": {}} |
| 8 | wait_until | pass | 0.52 | {} |
| 9 | hang_check | pass | 0.0 | {} |
| 10 | hang_check | pass | 0.0 | {} |
| 11 | save | pass | 0.0 | {"evidence_scope": "{'topology': 'single-PC loopback, two native seats', 'input': 'pad mailbox; no OS focus', 'boot': 'FM-00 rehearsal, not New Game acceptance', 'clips': '30-second two-screen blocks; encoding gaps are not continuous footage', 'caption_work': 'Burn in HOST / FRIEND and Single-PC loopback labels during final assembly; retain raw blocks', 'watchdog': 'gameplay hang checks off across native event holds; all steps have fixed deadlines', 'reaction_observation': 'Live08: existing read-only v7 promptHide from product-qualified actionable RC; raw cached RC remains diagnostic; routes/reach/timing/strategy unchanged'}"} |
| 12 | pilot | pass | 9.11 | {"exit": 0} |
| 13 | wait_until | pass | 0.06 | {} |
| 14 | save | pass | 0.0 | {"calling_deaths_before": "0"} |
| 15 | pilot | pass | 0.01 | {"background": true} |
| 16 | pilot | pass | 0.0 | {"background": true} |
| 17 | pilot_wait | pass | 133.22 | {"host_calling_path": {"exit": 0}, "friend_calling_path": {"exit": 0}} |
| 18 | assert | pass | 0.06 | {"value": "True"} |
| 19 | pilot | pass | 14.08 | {"exit": 0} |
| 20 | pilot | pass | 14.11 | {"exit": 0} |
| 21 | assert | pass | 0.03 | {"value": "True"} |
| 22 | pilot | pass | 2.11 | {"exit": 0} |
| 23 | wait_until | pass | 47.5 | {} |
| 24 | save | pass | 0.5 | {"thorn_ingress": "[{'location': [2, 34, 0, 157, 157, 157], 'mission': {'name': 'msn/us/TT34_MS304.bar', 'file': 'TT34_MS304.bar', 'flags': 1, 'result': 0, 'gauges': [{'flags': 1, 'max': 0, 'value': 0}, {'flags': 1, 'max': 0, 'value': 0}, {'flags': 1, 'max': 0, 'value': 0}], 'counters': [{'flags': 1, 'max': 0, 'value': 0}, {'flags': 1, 'max': 0, 'value': 0}, {'flags': 1, 'max': 0, 'value': 0}], 'timer': {'value': 0, 'state': 0, 'mode': 0}}, 'boss': [{'address': '0x7FF7EF0DFD10', 'name': 'B_EX100', 'objectId': 307, 'objectType': 12, 'team': 2, 'moveState': 8, 'motionId': 0, 'rotationY': 3.14159, 'motionTime': 34, 'flags': 0, 'modelFlags': 0, 'handler': 140702813071376, 'handlerVtableRva': 6105176, 'updateRva': 4298576, 'status': '0x7FF7EF5E30A8', 'hp': 390, 'maxHp': 390, 'position': {'x': 0, 'y': 0, 'z': 800}}], 'player': [{'address': '0x7FF7EF108130', 'name': 'P_EX110', 'objectId': 90, 'objectType': 0, 'team': 1, 'moveState': 2, 'motionId': 0, 'rotationY': -2.38419e-07, 'motionTime': 159, 'flags': 1048576, 'modelFlags': 0, 'handler': 140702813061888, 'handlerVtableRva': 6076968, 'updateRva': 4213312, 'status': '0x7FF7EF5E3320', 'hp': 20, 'maxHp': 20, 'position': {'x': -37.5, 'y': 0, 'z': -860}}]}, {'location': [2, 34, 0, 157, 157, 157], 'mission': {'name': 'msn/us/TT34_MS304.bar', 'file': 'TT34_MS304.bar', 'flags': 0, 'result': 0, 'gauges': [{'flags': 1, 'max': 0, 'value': 0}, {'flags': 1, 'max': 0, 'value': 0}, {'flags': 1, 'max': 0, 'value': 0}], 'counters': [{'flags': 1, 'max': 0, 'value': 0}, {'flags': 1, 'max': 0, 'value': 0}, {'flags': 1, 'max': 0, 'value': 0}], 'timer': {'value': 0, 'state': 0, 'mode': 0}}, 'boss': [{'address': '0x7FF7EF0DFD10', 'name': 'B_EX100', 'objectId': 307, 'objectType': 12, 'team': 2, 'moveState': 8, 'motionId': 0, 'rotationY': 3.14159, 'motionTime': 50, 'flags': 0, 'modelFlags': 0, 'handler': 140702813071376, 'handlerVtableRva': 6105176, 'updateRva': 4298576, 'status': '0x7FF7EF5E30A8', 'hp': 300, 'maxHp': 300, 'position': {'x': 0, 'y': 0, 'z': 800}}], 'player': [{'address': '0x7FF7EF108130', 'name': 'P_EX110', 'objectId': 90, 'objectType': 0, 'team': 1, 'moveState': 2, 'motionId': 0, 'rotationY': -2.38419e-07, 'motionTime': 26, 'flags': 1048576, 'modelFlags': 0, 'handler': 140702813061888, 'handlerVtableRva': 6076968, 'updateRva': 4213312, 'status': '0x7FF7EF5E3320', 'hp': 20, 'maxHp': 20, 'position': {'x': -107.5, 'y': 0, 'z': -860}}]}]"} |
| 25 | assert | pass | 0.0 | {"value": "True"} |
| 26 | wait_until | pass | 5.44 | {} |
| 27 | save | pass | 0.06 | {"thorn_baseline": "{'host_end': 0, 'friend_end': 0, 'victory_start': 0, 'claims': 39, 'downs': [0, 0], 'revives': [0, 0]}"} |
| 28 | clip | pass | 0.0 | {"path": "thorn_00_host_0.mp4", "background": true} |
| 29 | clip | pass | 0.0 | {"path": "thorn_00_friend_1.mp4", "background": true} |
| 30 | pilot | pass | 0.0 | {"background": true} |
| 31 | pilot | pass | 0.01 | {"background": true} |
| 32 | fight | pass | 34.53 | {} |
| 33 | clip_wait | pass | 28.89 | {"clips": [{"path": "thorn_00_host_0.mp4", "ok": true, "error": null}, {"path": "thorn_00_friend_1.mp4", "ok": true, "error": null}]} |
| 34 | assert | pass | 0.0 | {"value": "True"} |
| 35 | fight | pass | 61.05 | {"clips": [{"path": "thorn_01_host_0.mp4", "ok": true, "error": null}, {"path": "thorn_01_friend_1.mp4", "ok": true, "error": null}]} |
| 36 | assert | pass | 0.0 | {"value": "True"} |
| 37 | fight | pass | 62.72 | {"clips": [{"path": "thorn_02_host_0.mp4", "ok": true, "error": null}, {"path": "thorn_02_friend_1.mp4", "ok": true, "error": null}]} |
| 38 | assert | pass | 0.0 | {"value": "True"} |
| 39 | fight | pass | 56.23 | {"clips": [{"path": "thorn_03_host_0.mp4", "ok": true, "error": null}, {"path": "thorn_03_friend_1.mp4", "ok": true, "error": null}]} |
| 40 | assert | pass | 0.0 | {"value": "True"} |
| 41 | fight | pass | 60.23 | {"clips": [{"path": "thorn_04_host_0.mp4", "ok": true, "error": null}, {"path": "thorn_04_friend_1.mp4", "ok": true, "error": null}]} |
| 42 | assert | fail | 0.0 | An owned combat pilot failed; the current paired clip block is finalized. See host_thorn/friend_thorn pilot logs. Stop before recording another block. |

Artifacts: save_list_0.png, save_list_1.png, relay.log, runtime_0.log, runtime_1.log, host_to_calling.log, host_to_calling/doors.json, host_to_calling/modules.json, host_to_calling/pilot.jsonl, host_calling_path.log, friend_calling_path.log, host_calling_path/doors.json, host_calling_path/modules.json, host_calling_path/pilot.jsonl, friend_calling_path/doors.json, friend_calling_path/modules.json, friend_calling_path/pilot.jsonl, host_menu_tutorial.log, host_menu_tutorial/doors.json, host_menu_tutorial/menu_after_pages.png, host_menu_tutorial/menu_closed.png, host_menu_tutorial/menu_open.png, host_menu_tutorial/modules.json, host_menu_tutorial/pilot.jsonl, friend_menu_tutorial.log, friend_menu_tutorial/doors.json, friend_menu_tutorial/menu_after_pages.png, friend_menu_tutorial/menu_closed.png, friend_menu_tutorial/menu_open.png, friend_menu_tutorial/modules.json, friend_menu_tutorial/pilot.jsonl, host_to_awakening.log, host_to_awakening/doors.json, host_to_awakening/modules.json, host_to_awakening/pilot.jsonl, host_thorn.log, friend_thorn.log, thorn_00_samples.json, thorn_00_host_0.mp4, thorn_00_friend_1.mp4, thorn_01_samples.json, thorn_01_host_0.mp4, thorn_01_friend_1.mp4, thorn_02_samples.json, thorn_02_host_0.mp4, thorn_02_friend_1.mp4, thorn_03_samples.json, thorn_03_host_0.mp4, thorn_03_friend_1.mp4, thorn_04_samples.json, thorn_04_host_0.mp4, thorn_04_friend_1.mp4, desync-reports, desync-reports/88d9fe35e873bbecb786add21881523d/1, desync-reports/88d9fe35e873bbecb786add21881523d/2
