const fs = require('node:fs')
const { Parser } = require('mlg-converter');
const assert = require('node:assert');

const c = s => s.charCodeAt(0);
const s = (st, size) => [...st.split('').map(c), ...(size ? Array.from({ length: size - st.length }).fill(0x0) : [])]

const arrbToString = arrb => Array.prototype.map.call(arrb, (x, i) =>
  '0x' + x.toString(16).padStart(2, '0').toUpperCase()
).reduce((acc, hex, i) => {
  if (i % 8 === 0) {
    acc += `\n/* ${i.toString().padStart(3, ' ')} */    `;
  }
  if ((i + 1) % 4 === 0) {
    return acc + hex + ',    ';
  } else if ((i + 1) % 8 === 0) {
    return acc + hex + ',\n';
  } else {
    return acc + hex + ', ';
  }
}, '').trim()

const uint_t = (bytes) => (num) => {
  const byteArray = new Uint8Array(bytes);
  for (let i = 0; i < byteArray.length; i++) {
    byteArray[bytes - 1 - i] = (num >> (8 * i)) & 0xFF;
  }
  return byteArray;
};

const uint32_t = uint_t(4);
const uint16_t = uint_t(2);

const loggerField1 = [
  // Type: 0=U08, 1=S08, 2=U16, 3=S16, 4=U32, 5=S32, 6=S64, 7=F32
  0x0,
  // Name 34b
  ...s('RPM', 34),
  // Units 10b
  ...s('rpm', 10),
  // Display Style // 0=Float, 1=Hex, 2=bits, 3=Date, 4=On/Off, 5=Yes/No, 6=High/Low, 7=Active/Inactive, 8=True/False
  0x0,
  // scale / A IEEE 754 float representing the scale applied to (raw+transform)
  0x3F,
  0x80,
  0x0,
  0x0,
  // transform / A IEEE 754 float representing any shift of raw value before scaling
  ...uint32_t(0),
  // digits / S08 representing the number of decimal places to display to the right
  0x0,
  // Category 34b / Optional Category of this field. Provides logical grouping in MLV
  ...s('cat1', 34),
]

const loggerField2 = [
  // Type: 0=U08, 1=S08, 2=U16, 3=S16, 4=U32, 5=S32, 6=S64, 7=F32
  0x0,
  // Name 34b
  ...s('Test1', 34),
  // Units 10b
  ...s('parrots', 10),
  // Display Style // 0=Float, 1=Hex, 2=bits, 3=Date, 4=On/Off, 5=Yes/No, 6=High/Low, 7=Active/Inactive, 8=True/False
  0x0,
  // scale / A IEEE 754 float representing the scale applied to (raw+transform)
  0x3F,
  0x80,
  0x0,
  0x0,
  // transform / A IEEE 754 float representing any shift of raw value before scaling
  ...uint32_t(0),
  // digits / S08 representing the number of decimal places to display to the right
  0x0,
  // Category 34b / Optional Category of this field. Provides logical grouping in MLV
  ...s('cat1', 34),
]

let frameCounter = 0
let time = Math.floor(Date.now() / 1000)
const createFrame = (valueBytes) => {
  const blockType = 0x0
  const counter = frameCounter % 255
  const header = [
    blockType,
    counter,
    ...uint16_t(time),
  ]
  const crc = valueBytes.reduce((acc, it) => (acc + it) & 0xFF, 0)

  frameCounter++;
  time += 10;

  return [
    ...header,
    ...valueBytes,
    crc,
  ]
}

const fields = [
  loggerField1,
  loggerField2,
]

fields.forEach(it => assert(it.length === 89))

const data = Array.from({length: 20}, (it, i) => {
  return createFrame([i * 10, 100 - i * 2])
})
console.log('Data:');
for (const it of data) {
  console.log(arrbToString(it));
  console.log()
}

const infoData = s('This is sample info data!')
console.log('InfoData:');
console.log(arrbToString(infoData));

const arrb = new Uint8ClampedArray(
  [
    /** 0-5 File Format */
    ...s('MLVLG'), 0x0,

    /** 6-7 Version */
    ...uint16_t(2),

    /** 8-11 Time Stamp */
    ...uint32_t(Math.floor(Date.now() / 1000)),

    /** 12-15 Info Data Start */
    ...uint32_t(
      (24 + fields.length * 89) +
      (0 /* Bit Field Names */)
    ),

    /** 16-19 Data Begin Index */
    ...uint32_t(
      (24 + fields.length * 89) +
      (0 /* Bit Field Names */) +
      (infoData.length + 1)
    ),

    /** 20-21 Record Length */
    ...uint16_t(fields.length), // TODO: compute it from fields

    /** 22-23 Num Logger Fields */
    ...uint16_t(fields.length),

    /** Logger Field [] */
    ...fields.flat(),

    /** Bit Field Names */

    /** Info Data / Optional null terminated String */
    ...infoData, 0x0,

    /** Data */
    ...data.flat().flat(),
  ]
)

console.log('Result:');
console.log(arrbToString(arrb));

fs.writeFileSync('./test.mlg', arrb)
const b = fs.readFileSync('./test.mlg');
const arrayBuffer = b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength);
const result = new Parser(arrayBuffer).parse((percent) => console.log(percent));

console.dir(result, { maxArrayLength: 10 })
