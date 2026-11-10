// The base car of the editor: a plank on four wheels, to start a car from
// nothing.  Its wheels hang as those of the game's cars do, from the same
// points; the plank is braced so that it does not fold.

export const BASE_CAR = `section header
drive all
unknown06 0x82

section vertices
vertex[0] = (-0.203125, -0.3125, -0.0625) radius 0.1015625 wheel front
vertex[1] = (0.203125, -0.3125, -0.0625) radius 0.1015625 wheel front
vertex[2] = (0.203125, 0.2734375, -0.0625) radius 0.1015625 wheel back
vertex[3] = (-0.203125, 0.2734375, -0.0625) radius 0.1015625 wheel back
vertex[4] = (0.19921875, 0.46875, 0.0078125) radius 0 body
vertex[5] = (-0.19921875, 0.46875, 0.0078125) radius 0 body
vertex[6] = (-0.18359375, -0.41796875, 0.13671875) radius 0 body
vertex[7] = (0.18359375, -0.41796875, 0.13671875) radius 0 body
vertex[8] = (0.1953125, 0.46875, 0.1484375) radius 0 body
vertex[9] = (-0.1953125, 0.46875, 0.1484375) radius 0 body
vertex[10] = (0.203125, 0.109375, 0.15625) radius 0 body
vertex[11] = (-0.203125, 0.109375, 0.15625) radius 0 body
vertex[12] = (-0.2109375, -0.15625, -0.0078125) radius 0 body
vertex[13] = (0.2109375, -0.15625, -0.0078125) radius 0 body
vertex[14] = (-0.2109375, 0.109375, -0.0078125) radius 0 body
vertex[15] = (0.2109375, 0.109375, -0.0078125) radius 0 body
vertex[16] = (-0.18359375, -0.41796875, 0.03515625) radius 0 body
vertex[17] = (0.18359375, -0.41796875, 0.03515625) radius 0 body
vertex[18] = (-0.1171875, -0.3515625, -0.01953125) radius 0 body
vertex[19] = (0.1171875, -0.3515625, -0.01953125) radius 0 body
vertex[20] = (-0.08984375, 0.06640625, 0.201171875) radius 0.03125 reference
vertex[21] = (-0.1953125, -0.3125, 0.15234375) radius 0 body
vertex[22] = (0.1953125, -0.3125, 0.15234375) radius 0 body

section springs
spring vertex[12] - vertex[14]: type 1 shift 0 rest 0.265625 last 0.265625 min 0.1328125 max 0.3984375
spring vertex[13] - vertex[15]: type 1 shift 0 rest 0.265625 last 0.265625 min 0.1328125 max 0.3984375
spring vertex[12] - vertex[13]: type 1 shift 0 rest 0.421875 last 0.421875 min 0.2109375 max 0.6328125
spring vertex[14] - vertex[15]: type 1 shift 0 rest 0.421875 last 0.421875 min 0.2109375 max 0.6328125
spring vertex[14] - vertex[5]: type 1 shift 0 rest 0.35992431640625 last 0.35992431640625 min 0.17999267578125 max 0.5399169921875
spring vertex[15] - vertex[4]: type 1 shift 0 rest 0.35992431640625 last 0.35992431640625 min 0.17999267578125 max 0.5399169921875
spring vertex[5] - vertex[4]: type 1 shift 0 rest 0.3984375 last 0.3984375 min 0.19921875 max 0.59765625
spring vertex[11] - vertex[9]: type 1 shift 0 rest 0.35955810546875 last 0.35955810546875 min 0.1798095703125 max 0.53936767578125
spring vertex[10] - vertex[8]: type 1 shift 0 rest 0.35955810546875 last 0.35955810546875 min 0.1798095703125 max 0.53936767578125
spring vertex[11] - vertex[10]: type 1 shift 0 rest 0.40625 last 0.40625 min 0.203125 max 0.609375
spring vertex[9] - vertex[8]: type 1 shift 0 rest 0.390625 last 0.390625 min 0.1953125 max 0.5859375
spring vertex[5] - vertex[9]: type 1 shift 0 rest 0.14068603515625 last 0.14068603515625 min 0.07037353515625 max 0.2110595703125
spring vertex[4] - vertex[8]: type 1 shift 0 rest 0.14068603515625 last 0.14068603515625 min 0.07037353515625 max 0.2110595703125
spring vertex[6] - vertex[7]: type 1 shift 0 rest 0.3671875 last 0.3671875 min 0.18359375 max 0.55078125
spring vertex[18] - vertex[12]: type 1 shift 0 rest 0.21697998046875 last 0.21697998046875 min 0.1085205078125 max 0.32550048828125
spring vertex[19] - vertex[13]: type 1 shift 0 rest 0.21697998046875 last 0.21697998046875 min 0.1085205078125 max 0.32550048828125
spring vertex[18] - vertex[19]: type 1 shift 0 rest 0.234375 last 0.234375 min 0.1171875 max 0.3515625
spring vertex[15] - vertex[10]: type 1 shift 0 rest 0.16424560546875 last 0.16424560546875 min 0.0821533203125 max 0.24639892578125
spring vertex[14] - vertex[11]: type 1 shift 0 rest 0.16424560546875 last 0.16424560546875 min 0.0821533203125 max 0.24639892578125
spring vertex[16] - vertex[17]: type 1 shift 0 rest 0.3671875 last 0.3671875 min 0.18359375 max 0.55078125
spring vertex[16] - vertex[6]: type 1 shift 0 rest 0.1015625 last 0.1015625 min 0.05078125 max 0.15234375
spring vertex[17] - vertex[7]: type 1 shift 0 rest 0.1015625 last 0.1015625 min 0.05078125 max 0.15234375
spring vertex[16] - vertex[18]: type 1 shift 0 rest 0.10870361328125 last 0.10870361328125 min 0.05438232421875 max 0.1630859375
spring vertex[17] - vertex[19]: type 1 shift 0 rest 0.10870361328125 last 0.10870361328125 min 0.05438232421875 max 0.1630859375
spring vertex[12] - vertex[11]: type 1 shift 0 rest 0.31231689453125 last 0.31231689453125 min 0.15618896484375 max 0.468505859375
spring vertex[13] - vertex[10]: type 1 shift 0 rest 0.31231689453125 last 0.31231689453125 min 0.15618896484375 max 0.468505859375
spring vertex[12] - vertex[15]: type 1 shift 0 rest 0.49853515625 last 0.49853515625 min 0.249267578125 max 0.747802734375
spring vertex[13] - vertex[14]: type 1 shift 0 rest 0.49853515625 last 0.49853515625 min 0.249267578125 max 0.747802734375
spring vertex[18] - vertex[13]: type 1 shift 0 rest 0.38201904296875 last 0.38201904296875 min 0.1910400390625 max 0.57305908203125
spring vertex[19] - vertex[12]: type 1 shift 0 rest 0.38201904296875 last 0.38201904296875 min 0.1910400390625 max 0.57305908203125
spring vertex[6] - vertex[12]: type 1 shift 0 rest 0.30023193359375 last 0.30023193359375 min 0.150146484375 max 0.45037841796875
spring vertex[7] - vertex[13]: type 1 shift 0 rest 0.30023193359375 last 0.30023193359375 min 0.150146484375 max 0.45037841796875
spring vertex[14] - vertex[4]: type 1 shift 0 rest 0.5455322265625 last 0.5455322265625 min 0.27276611328125 max 0.81829833984375
spring vertex[15] - vertex[5]: type 1 shift 0 rest 0.5455322265625 last 0.5455322265625 min 0.27276611328125 max 0.81829833984375
spring vertex[11] - vertex[8]: type 1 shift 0 rest 0.53662109375 last 0.53662109375 min 0.268310546875 max 0.804931640625
spring vertex[10] - vertex[9]: type 1 shift 0 rest 0.53662109375 last 0.53662109375 min 0.268310546875 max 0.804931640625
spring vertex[5] - vertex[8]: type 1 shift 0 rest 0.4188232421875 last 0.4188232421875 min 0.20941162109375 max 0.62823486328125
spring vertex[4] - vertex[9]: type 1 shift 0 rest 0.4188232421875 last 0.4188232421875 min 0.20941162109375 max 0.62823486328125
spring vertex[14] - vertex[9]: type 1 shift 0 rest 0.3922119140625 last 0.3922119140625 min 0.19610595703125 max 0.58831787109375
spring vertex[15] - vertex[8]: type 1 shift 0 rest 0.3922119140625 last 0.3922119140625 min 0.19610595703125 max 0.58831787109375
spring vertex[10] - vertex[4]: type 1 shift 0 rest 0.38885498046875 last 0.38885498046875 min 0.1944580078125 max 0.58331298828125
spring vertex[11] - vertex[5]: type 1 shift 0 rest 0.38885498046875 last 0.38885498046875 min 0.1944580078125 max 0.58331298828125
spring vertex[14] - vertex[10]: type 1 shift 0 rest 0.44537353515625 last 0.44537353515625 min 0.22271728515625 max 0.6680908203125
spring vertex[15] - vertex[11]: type 1 shift 0 rest 0.44537353515625 last 0.44537353515625 min 0.22271728515625 max 0.6680908203125
spring vertex[19] - vertex[7]: type 1 shift 0 rest 0.18231201171875 last 0.18231201171875 min 0.0911865234375 max 0.27349853515625
spring vertex[18] - vertex[6]: type 1 shift 0 rest 0.18231201171875 last 0.18231201171875 min 0.0911865234375 max 0.27349853515625
spring vertex[18] - vertex[7]: type 1 shift 0 rest 0.34539794921875 last 0.34539794921875 min 0.1727294921875 max 0.51812744140625
spring vertex[19] - vertex[6]: type 1 shift 0 rest 0.34539794921875 last 0.34539794921875 min 0.1727294921875 max 0.51812744140625
spring vertex[3] - vertex[2]: type 1 shift 0 rest 0.40625 last 0.40625 min 0.203125 max 0.609375
spring vertex[3] - vertex[14]: type 1 shift 0 rest 0.173095703125 last 0.173095703125 min 0.0865478515625 max 0.2596435546875
spring vertex[2] - vertex[15]: type 1 shift 0 rest 0.173095703125 last 0.173095703125 min 0.0865478515625 max 0.2596435546875
spring vertex[3] - vertex[15]: type 1 shift 0 rest 0.44873046875 last 0.44873046875 min 0.224365234375 max 0.673095703125
spring vertex[14] - vertex[2]: type 1 shift 0 rest 0.44873046875 last 0.44873046875 min 0.224365234375 max 0.673095703125
spring vertex[21] - vertex[18]: type 1 shift 0 rest 0.19281005859375 last 0.19281005859375 min 0.096435546875 max 0.28924560546875
spring vertex[22] - vertex[19]: type 1 shift 0 rest 0.19281005859375 last 0.19281005859375 min 0.096435546875 max 0.28924560546875
spring vertex[21] - vertex[12]: type 1 shift 0 rest 0.22430419921875 last 0.22430419921875 min 0.1121826171875 max 0.33648681640625
spring vertex[22] - vertex[13]: type 1 shift 0 rest 0.22430419921875 last 0.22430419921875 min 0.1121826171875 max 0.33648681640625
spring vertex[22] - vertex[21]: type 1 shift 0 rest 0.390625 last 0.390625 min 0.1953125 max 0.5859375
spring vertex[21] - vertex[6]: type 1 shift 0 rest 0.10723876953125 last 0.10723876953125 min 0.05364990234375 max 0.160888671875
spring vertex[22] - vertex[7]: type 1 shift 0 rest 0.10723876953125 last 0.10723876953125 min 0.05364990234375 max 0.160888671875
spring vertex[20] - vertex[12]: type 1 shift 0 rest 0.3284912109375 last 0.3284912109375 min 0.16424560546875 max 0.49273681640625
spring vertex[20] - vertex[13]: type 1 shift 0 rest 0.42864990234375 last 0.42864990234375 min 0.21435546875 max 0.64300537109375
spring vertex[20] - vertex[14]: type 1 shift 0 rest 0.24530029296875 last 0.24530029296875 min 0.1226806640625 max 0.36798095703125
spring vertex[20] - vertex[15]: type 1 shift 0 rest 0.3687744140625 last 0.3687744140625 min 0.18438720703125 max 0.55316162109375
spring vertex[7] - vertex[16]: type 1 shift 0 rest 0.3809814453125 last 0.3809814453125 min 0.19049072265625 max 0.57147216796875
spring vertex[6] - vertex[17]: type 1 shift 0 rest 0.3809814453125 last 0.3809814453125 min 0.19049072265625 max 0.57147216796875
spring vertex[0] - vertex[13]: type 1 shift 0 rest 0.4459228515625 last 0.4459228515625 min 0.22296142578125 max 0.66888427734375
spring vertex[1] - vertex[12]: type 1 shift 0 rest 0.4459228515625 last 0.4459228515625 min 0.22296142578125 max 0.66888427734375
spring vertex[21] - vertex[16]: type 1 shift 0 rest 0.1580810546875 last 0.1580810546875 min 0.07904052734375 max 0.23712158203125
spring vertex[22] - vertex[17]: type 1 shift 0 rest 0.1580810546875 last 0.1580810546875 min 0.07904052734375 max 0.23712158203125
spring vertex[22] - vertex[6]: type 1 shift 0 rest 0.39361572265625 last 0.39361572265625 min 0.19683837890625 max 0.5904541015625
spring vertex[21] - vertex[7]: type 1 shift 0 rest 0.39361572265625 last 0.39361572265625 min 0.19683837890625 max 0.5904541015625
spring vertex[12] - vertex[22]: type 1 shift 0 rest 0.46380615234375 last 0.46380615234375 min 0.23193359375 max 0.69573974609375
spring vertex[13] - vertex[21]: type 1 shift 0 rest 0.46380615234375 last 0.46380615234375 min 0.23193359375 max 0.69573974609375
spring vertex[0] - vertex[21]: type 12 shift 0 rest 0.2197265625 last 0.2197265625 min 0.10748291015625 max 0.2197265625
spring vertex[1] - vertex[22]: type 12 shift 0 rest 0.2197265625 last 0.2197265625 min 0.10748291015625 max 0.2197265625
spring vertex[3] - vertex[11]: type 4 shift 0 rest 0.274658203125 last 0.274658203125 min 0.13671875 max 0.274658203125
spring vertex[2] - vertex[10]: type 4 shift 0 rest 0.274658203125 last 0.274658203125 min 0.13671875 max 0.274658203125
spring vertex[18] - vertex[0]: type 0 shift 0 rest 0.10369873046875 last 0.10369873046875 min 0.0518798828125 max 0.15557861328125
spring vertex[19] - vertex[1]: type 0 shift 0 rest 0.10369873046875 last 0.10369873046875 min 0.0518798828125 max 0.15557861328125
spring vertex[5] - vertex[3]: type 0 shift 0 rest 0.2076416015625 last 0.2076416015625 min 0.10382080078125 max 0.31146240234375
spring vertex[4] - vertex[2]: type 0 shift 0 rest 0.2076416015625 last 0.2076416015625 min 0.10382080078125 max 0.31146240234375
spring vertex[0] - vertex[17]: type 1 shift 0 rest 0.41259765625 last 0.41259765625 min 0.206298828125 max 0.618896484375
spring vertex[1] - vertex[16]: type 1 shift 0 rest 0.41259765625 last 0.41259765625 min 0.206298828125 max 0.618896484375
spring vertex[16] - vertex[12]: type 1 shift 0 rest 0.2666015625 last 0.2666015625 min 0.13330078125 max 0.39990234375
spring vertex[17] - vertex[13]: type 1 shift 0 rest 0.2666015625 last 0.2666015625 min 0.13330078125 max 0.39990234375
spring vertex[22] - vertex[10]: type 1 shift 0 rest 0.42193603515625 last 0.42193603515625 min 0.2109375 max 0.6329345703125
spring vertex[11] - vertex[21]: type 1 shift 0 rest 0.42193603515625 last 0.42193603515625 min 0.2109375 max 0.6329345703125
spring vertex[6] - vertex[4]: type 1 shift 0 rest 0.974365234375 last 0.974365234375 min 0.4871826171875 max 1.4615478515625
spring vertex[7] - vertex[5]: type 1 shift 0 rest 0.974365234375 last 0.974365234375 min 0.4871826171875 max 1.4615478515625
spring vertex[9] - vertex[17]: type 1 shift 0 rest 0.97088623046875 last 0.97088623046875 min 0.4854736328125 max 1.456298828125
spring vertex[8] - vertex[16]: type 1 shift 0 rest 0.97088623046875 last 0.97088623046875 min 0.4854736328125 max 1.456298828125
spring vertex[21] - vertex[10]: type 1 shift 0 rest 0.58026123046875 last 0.58026123046875 min 0.2901611328125 max 0.870361328125
spring vertex[22] - vertex[11]: type 1 shift 0 rest 0.58026123046875 last 0.58026123046875 min 0.2901611328125 max 0.870361328125
spring vertex[16] - vertex[13]: type 1 shift 0 rest 0.475341796875 last 0.475341796875 min 0.2376708984375 max 0.7130126953125
spring vertex[17] - vertex[12]: type 1 shift 0 rest 0.475341796875 last 0.475341796875 min 0.2376708984375 max 0.7130126953125

section program
    flat color 0x0000: shadow[15], shadow[14], shadow[5], shadow[4], vertex[0]
    flat color 0x0000: shadow[13], shadow[12], shadow[14], shadow[15], vertex[0]
    flat color 0x0000: shadow[19], shadow[18], shadow[12], shadow[13], vertex[0]
    flat color 0x0000: shadow[17], shadow[16], shadow[18], shadow[19], vertex[0]
    flat color 0x1c1c: vertex[6], vertex[7], vertex[22], vertex[21], vertex[6]
    flat color 0x1c1c: vertex[21], vertex[22], vertex[10], vertex[11], vertex[21]
    flat color 0x1c1c: vertex[11], vertex[10], vertex[8], vertex[9], vertex[11]
    flat color 0x4343: vertex[12], vertex[13], vertex[17], vertex[16], vertex[12]
    flat color 0x4343: vertex[14], vertex[15], vertex[13], vertex[12], vertex[14]
    flat color 0x4343: vertex[5], vertex[4], vertex[15], vertex[14], vertex[5]
    flat color 0x1616: vertex[16], vertex[17], vertex[7], vertex[6], vertex[16]
    flat color 0x1616: vertex[9], vertex[8], vertex[4], vertex[5], vertex[9]
    flat color 0x1717: vertex[21], vertex[12], vertex[16], vertex[6], vertex[21]
    flat color 0x1717: vertex[11], vertex[14], vertex[12], vertex[21], vertex[11]
    flat color 0x1717: vertex[9], vertex[5], vertex[14], vertex[11], vertex[9]
    flat color 0x1717: vertex[7], vertex[17], vertex[13], vertex[22], vertex[7]
    flat color 0x1717: vertex[22], vertex[13], vertex[15], vertex[10], vertex[22]
    flat color 0x1717: vertex[10], vertex[15], vertex[4], vertex[8], vertex[10]
    wheel vertex[0], axle[0]
        view 6646 6646: (118.5, 2.5) (148.5, 2.5) (148.5, 32.5) (118.5, 32.5)
        view 6646 6149: (150.5, 2.5) (178.5, 2.5) (178.5, 32.5) (150.5, 32.5)
        view 6646 5396: (180.5, 2.5) (205.5, 2.5) (205.5, 32.5) (180.5, 32.5)
        view 6646 3886: (207.5, 2.5) (225.5, 2.5) (225.5, 32.5) (207.5, 32.5)
        view 6646 2386: (227.5, 2.5) (238.5, 2.5) (238.5, 32.5) (227.5, 32.5)
        view 6646 3886: (206.5, 34.5) (224.5, 34.5) (224.5, 63.5) (206.5, 63.5)
        view 6646 5396: (179.5, 34.5) (204.5, 34.5) (204.5, 63.5) (179.5, 63.5)
        view 6646 6149: (149.5, 34.5) (177.5, 34.5) (177.5, 63.5) (149.5, 63.5)
        view 6646 6646: (118.5, 34.5) (147.5, 34.5) (147.5, 63.5) (118.5, 63.5)
    wheel vertex[1], axle[1]
        view 6646 6646: (118.5, 2.5) (148.5, 2.5) (148.5, 32.5) (118.5, 32.5)
        view 6646 6149: (150.5, 2.5) (178.5, 2.5) (178.5, 32.5) (150.5, 32.5)
        view 6646 5396: (180.5, 2.5) (205.5, 2.5) (205.5, 32.5) (180.5, 32.5)
        view 6646 3886: (207.5, 2.5) (225.5, 2.5) (225.5, 32.5) (207.5, 32.5)
        view 6646 2386: (227.5, 2.5) (238.5, 2.5) (238.5, 32.5) (227.5, 32.5)
        view 6646 3886: (206.5, 34.5) (224.5, 34.5) (224.5, 63.5) (206.5, 63.5)
        view 6646 5396: (179.5, 34.5) (204.5, 34.5) (204.5, 63.5) (179.5, 63.5)
        view 6646 6149: (149.5, 34.5) (177.5, 34.5) (177.5, 63.5) (149.5, 63.5)
        view 6646 6646: (118.5, 34.5) (147.5, 34.5) (147.5, 63.5) (118.5, 63.5)
    wheel vertex[2], vertex[3]
        view 6646 6646: (118.5, 2.5) (148.5, 2.5) (148.5, 32.5) (118.5, 32.5)
        view 6646 6149: (150.5, 2.5) (178.5, 2.5) (178.5, 32.5) (150.5, 32.5)
        view 6646 5396: (180.5, 2.5) (205.5, 2.5) (205.5, 32.5) (180.5, 32.5)
        view 6646 3886: (207.5, 2.5) (225.5, 2.5) (225.5, 32.5) (207.5, 32.5)
        view 6646 2386: (227.5, 2.5) (238.5, 2.5) (238.5, 32.5) (227.5, 32.5)
        view 6646 3886: (206.5, 34.5) (224.5, 34.5) (224.5, 63.5) (206.5, 63.5)
        view 6646 5396: (179.5, 34.5) (204.5, 34.5) (204.5, 63.5) (179.5, 63.5)
        view 6646 6149: (149.5, 34.5) (177.5, 34.5) (177.5, 63.5) (149.5, 63.5)
        view 6646 6646: (118.5, 34.5) (147.5, 34.5) (147.5, 63.5) (118.5, 63.5)
    wheel vertex[3], vertex[2]
        view 6646 6646: (118.5, 2.5) (148.5, 2.5) (148.5, 32.5) (118.5, 32.5)
        view 6646 6149: (150.5, 2.5) (178.5, 2.5) (178.5, 32.5) (150.5, 32.5)
        view 6646 5396: (180.5, 2.5) (205.5, 2.5) (205.5, 32.5) (180.5, 32.5)
        view 6646 3886: (207.5, 2.5) (225.5, 2.5) (225.5, 32.5) (207.5, 32.5)
        view 6646 2386: (227.5, 2.5) (238.5, 2.5) (238.5, 32.5) (227.5, 32.5)
        view 6646 3886: (206.5, 34.5) (224.5, 34.5) (224.5, 63.5) (206.5, 63.5)
        view 6646 5396: (179.5, 34.5) (204.5, 34.5) (204.5, 63.5) (179.5, 63.5)
        view 6646 6149: (149.5, 34.5) (177.5, 34.5) (177.5, 63.5) (149.5, 63.5)
        view 6646 6646: (118.5, 34.5) (147.5, 34.5) (147.5, 63.5) (118.5, 63.5)
    end
`;
