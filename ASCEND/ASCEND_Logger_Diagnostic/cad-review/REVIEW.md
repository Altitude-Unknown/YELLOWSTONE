# ASCEND Logger GPS CAD review — October 26, 2026

Read-only review of the supplied EAGLE schematic and board XML. Copies are
preserved here; Downloads originals were not edited. Board uses two copper
layers (1/16); configured main dielectric thickness 1.5 mm.

## Confirmed supply issue

U2 pin 22 (`V_BCKP`) is unconnected in both schematic and PCB. The entire
board signal is:

```xml
<signal name="V-BACKUP">
  <contactref element="U2" pad="22" />
</signal>
```

u-blox's NEO-M9N integration manual R10, section 4.2.2/page 71 says to
connect V_BCKP to VCC when no backup supply is available. Main VCC (pin 23)
is already on +3V3. First bench correction: with all power disconnected,
connect pin 22 to the same +3V3 supply as pin 23, inspect and verify the
connection before applying power, then repeat an outdoor satellite test.
This is a documented design correction, not proof that it alone causes the
observed reception failure. No physical modification has been performed by
the assistant.

The unused GPS USB supply (pin 7) is also on +3V3. u-blox section 4.2.3 says
to ground V_USB when GPS USB is unused. Treat this as another schematic
correction to review; do not short the current +3V3 net to ground. Changing
it on an existing board would require isolation of its present connection.

Source:
https://content.u-blox.com/sites/default/files/NEO-M9N_Integrationmanual_UBX-19014286.pdf

## RF path

- `GPS_ANT1` correctly connects U2 pad 11, AE1 pad 1, and C10 pad 1.
- C10 pad 2 is ground; CAD specifies 2.2 pF. Fitted value has not been verified.
- U2 pad 11 to AE1 pad 1 trace is approximately 4.27 mm long, width 0.8128 mm.
- C10 is on a branch approximately 5.40 mm long, width 0.3048 mm. A shorter
  matching-component connection is preferable in a future RF layout review;
  no impedance or RF simulation was performed. Nominal trace width alone
  cannot establish impedance without the manufactured stackup and grounds.
- No RF-net vias occur in this path.

## Antenna footprint: correction to initial commentary

Do NOT classify the three-land footprint as wrong or conclude it shorts the
RF signal just because there are two PCB lands at one antenna end. Pulse's
2008 W3011 datasheet explicitly specifies PCB lands 1=feed, 2=ground,
3=ground, with 0.8x0.65, 0.8x0.65 and 0.8x1.60 mm dimensions. The library's
pad sizes match these after rotation. The assistant initially flagged the
three-land pattern, then corrected that claim after consulting this drawing.
No footprint change or ground-pad disconnection is justified by that claim.

Manufacturer drawing hosted by SparkFun:
https://cdn.sparkfun.com/assets/learn_tutorials/8/8/1/W3011_W3011A.pdf

## Copper clearance

Top cutout polygon bounds: x=77.1062..79.2438, y=-1.5438..1.5038 mm:
approximately 2.138 x 3.048 mm.

Bottom cutout polygon bounds: x=77.0862..79.2638, y=-1.5338..1.4938 mm:
approximately 2.178 x 3.028 mm.

Pulse recommends a 4.00 x 4.25 mm clearance region for W3011, with prescribed
contact/feed copper exceptions. The supplied cutouts are smaller. Polygon
bounds are CAD measurements, not measurements of the fabricated copper or
recomputed pour geometry. The board edge also intersects the cutout area.
Review full top/bottom copper, placement and matching against the Pulse
reference before another PCB revision. Do not assume enlarging clearance
alone guarantees reception on this board size/antenna placement.

Current manufacturer reference:
https://productfinder.pulseeng.com/doc_type/WEB301/doc_num/W3011/doc_part/W3011.pdf

## Test status preserved

SD write/read passed; MS5611 basic measurement function passed. GPS I2C,
UBX navigation reads and enabled constellation checks passed, but no usable
satellite signals or valid fix were obtained in the reported outdoor tests.
User subsequently brought board indoors. No new firmware or hardware changes
were made during this CAD review.
