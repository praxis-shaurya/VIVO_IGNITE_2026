# AVROD — Retrospective Project Timeline

> **Purpose:** A personal record of how the project evolved. This is a retrospective assembled from memory, not a claim that every event date is independently verified.
>
> **Timeline rule:** Dates are labelled as recalled, approximate, or uncertain where appropriate. The Git commit date for this file should be the actual date it is added to a repository; historical milestones are documented here rather than disguised as commits made at the time.

## Milestones

### Around 26–27 June 2026 — The idea takes shape
- While researching sewer-cleaning mishaps shortly before the Vivo Ignite submission, the Mundka incident became the emotional starting point for the project.
- The first concept focused on preventing unauthorised or unsafe manhole entry without the required safety conditions.
- The original name was **AVRODH — Automated Verification for Restricted Operator Descent into Hazard**. The name was generated with AI assistance.
- The proposal described a three-condition entry interlock: gas-sensor check, RFID verification of PPE, and municipal-authority authorisation.
- It also proposed attempt logging, alerts, monitoring during work, and a possible emergency-retraction mechanism. These were **proposal features**, not all confirmed as built.
- First sketches were made while completing the submission under deadline pressure.
- The original framing was intended to prevent unsafe entry and complement mechanisation, not replace mechanised cleaning.

**Date confidence:** The user recalls approximately 27 June for the idea/submission; exact submission date is not confirmed.

### 24 July 2026 — Vivo Ignite Top 200
- The user recalls learning of selection for the Top 200 on 24 July.
- After submitting, the user had paused project work while waiting for the result.

**Date confidence:** Recalled by the user.

### July–August 2026 — First physical prototype
- A first model was designed in Tinkercad and 3D-printed in two-colour PLA.
- School-borrowed components and purchased electronics were used to start the build.
- The first electronics iteration used a **NodeMCU / ESP8266, RFID reader, buzzer, and gas sensor**.
- **The first version did not include a physical locking system.**
- The project video was submitted on **21 August 2026**. The remembered format opened with melancholy piano and a narrated story of the Mundka incident before transitioning to the proposed solution.

**Date confidence:** Video date is recalled; exact dates of printing and first successful tests are unknown.

### 8 September 2026 — Top 30
- The user recalls the National Top 30 announcement on 8 September.

**Date confidence:** Recalled by the user.

### 15 September 2026 — First Achiever 30 mentoring session
- The mentor challenged the project's framing, warning that it could be interpreted as supporting an approach inconsistent with the law and the intended direction of safe sanitation.
- This triggered a major rethink rather than abandonment of the project.

**Date confidence:** Recalled by the user.

### Around 16 September 2026 — UMCD concept
- The user recalls beginning the **Universal Manhole Dock (UMCD)** concept on or around 16 September, the day after the first mentoring session.
- The guiding analogy was **“USB-C for cleaning devices”**: standardise the manhole-side interface so different mechanised cleaning systems can connect to or use common infrastructure.
- The concept included provision for winch installation and was intended to make mechanisation-first approaches easier to support.
- The wider concept began combining:
  - **UMCD:** common physical interface for mechanised cleaning.
  - **AVROD / smart manhole:** monitoring and safety-enforcement layer.
  - **Future data/AI layer:** network-level gas-pattern mapping and identifying potential hotspots from accumulated sensor data.

**Important distinction:** These were design concepts at this stage, not proof that a city-wide system or AI model had been built.

### 19 September 2026 — Reframing recognised
- In the second mentoring session, the mentor responded: **“NOW! You have worked upon my feedback!”**
- The mentor advised removing the **H** from AVRODH because “hazard” made the name sound negative.
- The project name became **AVROD**.

**Date confidence:** Recalled by the user.

### 5 October 2026 — National Top 10
- The user learned by email that AVROD had reached the **National Top 10**.
- The announcement became a memorable personal moment: the user had just woken from an afternoon nap when their brother announced the result with improvised dramatic sound effects and a pose inspired by the scale of the achievement.

**Date confidence:** Recalled by the user.

### 8–10 October 2026 — Acrylic rebuild and final prototype push
- The latest physical build was made over **8–10 October**.
- The user chose acrylic partly because the transparent material looked cool, and built directly rather than making a separate sketch first.
- The physical model is described by the user as a manhole model.
- The latest controller remained **NodeMCU / ESP8266**.
- Gas sensing and RFID/PPE-kit detection were reported as functioning in tests, but the gas sensor output has **no calibrated physical unit**. Readings should therefore be described as raw sensor values, not gas concentration in ppm.
- A servo was added as the physical lock. Its operation is **unreliable under force**, so the current mechanism does not yet provide dependable physical security.
- The dashboard/software was still being developed and, as of 11 October, **none of its intended functions were reported as working**.

**Date confidence:** The user says the acrylic build and latest work happened from 8 to 10 October.

### 11 October 2026 — Current state
- **Project status:** The user considers the project pretty much complete, with cosmetic work remaining.
- **Controller:** NodeMCU / ESP8266.
- **Sensing and identification:** Gas sensor and RFID/PPE-kit detection reported working; gas readings are uncalibrated raw values.
- **Physical lock:** Servo mechanism exists but is unreliable under force; this is a known engineering limitation, not a solved feature.
- **Dashboard:** Still nonfunctional / under development at this point.
- **Presentation:** The project is invited to Bharat Mandapam with the user's family and prototype for an event on **28 October 2026**, where the Top 5 ideas are to be felicitated, according to the invitation as recalled by the user.

## Design evolution at a glance

| Phase | Main idea | What changed |
|---|---|---|
| Initial proposal | AVRODH safety interlock | Prevent unsafe entry through gas, PPE and authorisation checks |
| First prototype | NodeMCU + gas sensor + RFID + buzzer | Began testing sensing and identification; no physical lock in V1 |
| Mentoring feedback | Mechanisation-first reframing | Recognised the need to avoid framing the system as enabling routine manual entry |
| UMCD | Universal Manhole Dock | Shifted part of the solution toward common infrastructure for mechanised cleaning |
| Combined concept | UMCD + AVROD + future data layer | Physical interface, safety layer, and possible network-level analysis became distinct layers |
| October build | Acrylic manhole model + servo lock | Physical lock added, but its force resistance remains unreliable; dashboard still unfinished |

## What not to overclaim

- The original proposal's emergency retraction, authority alerting, event logging, and other proposed capabilities should not be described as implemented unless separately verified.
- The gas sensor's raw output is not a calibrated concentration unit.
- RFID detection working does not by itself prove that the dashboard updates correctly or that every PPE item is verified.
- The servo moving is not equivalent to a dependable lock under force.
- The dashboard should be described as unfinished as of 11 October 2026.
- AI heatmaps and city-wide sewer analysis are future concepts unless later implemented and validated.
- The 28 October event is a planned event as of this timeline's date, not an event already attended.

## Suggested Git practice

Add this file in a **new commit made on the actual day you create it**. Keep the historical dates inside this document as retrospective milestones. If you later find dated photos, emails, source files, or videos, update the relevant confidence notes instead of inventing precision.

Suggested commit message:

`docs: record retrospective AVROD project timeline`
