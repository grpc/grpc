<?php
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: src/proto/grpc/testing/control.proto

namespace GPBMetadata\Src\Proto\Grpc\Testing;

class Control
{
    public static $is_initialized = false;

    public static function initOnce()
    {
        $pool = \Google\Protobuf\Internal\DescriptorPool::getGeneratedPool();

        if (static::$is_initialized == true) {
            return;
        }
        \GPBMetadata\Src\Proto\Grpc\Testing\Payloads::initOnce();
        \GPBMetadata\Src\Proto\Grpc\Testing\Stats::initOnce();
        $pool->internalAddGeneratedFile(hex2bin(
            '0aa21a0a247372632f70726f746f2f677270632f74657374696e672f636f'.
            '6e74726f6c2e70726f746f120c677270632e74657374696e671a22737263'.
            '2f70726f746f2f677270632f74657374696e672f73746174732e70726f74'.
            '6f22250a0d506f6973736f6e506172616d7312140a0c6f6666657265645f'.
            '6c6f616418012001280122120a10436c6f7365644c6f6f70506172616d73'.
            '227b0a0a4c6f6164506172616d7312350a0b636c6f7365645f6c6f6f7018'.
            '012001280b321e2e677270632e74657374696e672e436c6f7365644c6f6f'.
            '70506172616d734800122e0a07706f6973736f6e18022001280b321b2e67'.
            '7270632e74657374696e672e506f6973736f6e506172616d73480042060a'.
            '046c6f616422560a0e5365637572697479506172616d7312130a0b757365'.
            '5f746573745f6361180120012808121c0a147365727665725f686f73745f'.
            '6f7665727269646518022001280912110a09637265645f74797065180320'.
            '012809224d0a0a4368616e6e656c417267120c0a046e616d651801200128'.
            '0912130a097374725f76616c7565180220012809480012130a09696e745f'.
            '76616c7565180320012805480042070a0576616c756522d5040a0c436c69'.
            '656e74436f6e66696712160a0e7365727665725f74617267657473180120'.
            '032809122d0a0b636c69656e745f7479706518022001280e32182e677270'.
            '632e74657374696e672e436c69656e745479706512350a0f736563757269'.
            '74795f706172616d7318032001280b321c2e677270632e74657374696e67'.
            '2e5365637572697479506172616d7312240a1c6f75747374616e64696e67'.
            '5f727063735f7065725f6368616e6e656c18042001280512170a0f636c69'.
            '656e745f6368616e6e656c73180520012805121c0a146173796e635f636c'.
            '69656e745f7468726561647318072001280512270a087270635f74797065'.
            '18082001280e32152e677270632e74657374696e672e5270635479706512'.
            '2d0a0b6c6f61645f706172616d73180a2001280b32182e677270632e7465'.
            '7374696e672e4c6f6164506172616d7312330a0e7061796c6f61645f636f'.
            '6e666967180b2001280b321b2e677270632e74657374696e672e5061796c'.
            '6f6164436f6e66696712370a10686973746f6772616d5f706172616d7318'.
            '0c2001280b321d2e677270632e74657374696e672e486973746f6772616d'.
            '506172616d7312110a09636f72655f6c697374180d2003280512120a0a63'.
            '6f72655f6c696d6974180e2001280512180a106f746865725f636c69656e'.
            '745f617069180f20012809122e0a0c6368616e6e656c5f61726773181020'.
            '03280b32182e677270632e74657374696e672e4368616e6e656c41726712'.
            '160a0e746872656164735f7065725f6371181120012805121b0a136d6573'.
            '73616765735f7065725f73747265616d18122001280522380a0c436c6965'.
            '6e7453746174757312280a05737461747318012001280b32192e67727063'.
            '2e74657374696e672e436c69656e74537461747322150a044d61726b120d'.
            '0a05726573657418012001280822680a0a436c69656e7441726773122b0a'.
            '05736574757018012001280b321a2e677270632e74657374696e672e436c'.
            '69656e74436f6e666967480012220a046d61726b18022001280b32122e67'.
            '7270632e74657374696e672e4d61726b480042090a076172677479706522'.
            'fd020a0c536572766572436f6e666967122d0a0b7365727665725f747970'.
            '6518012001280e32182e677270632e74657374696e672e53657276657254'.
            '79706512350a0f73656375726974795f706172616d7318022001280b321c'.
            '2e677270632e74657374696e672e5365637572697479506172616d73120c'.
            '0a04706f7274180420012805121c0a146173796e635f7365727665725f74'.
            '68726561647318072001280512120a0a636f72655f6c696d697418082001'.
            '280512330a0e7061796c6f61645f636f6e66696718092001280b321b2e67'.
            '7270632e74657374696e672e5061796c6f6164436f6e66696712110a0963'.
            '6f72655f6c697374180a2003280512180a106f746865725f736572766572'.
            '5f617069180b2001280912160a0e746872656164735f7065725f6371180c'.
            '20012805121c0a137265736f757263655f71756f74615f73697a6518e907'.
            '20012805122f0a0c6368616e6e656c5f6172677318ea072003280b32182e'.
            '677270632e74657374696e672e4368616e6e656c41726722680a0a536572'.
            '76657241726773122b0a05736574757018012001280b321a2e677270632e'.
            '74657374696e672e536572766572436f6e666967480012220a046d61726b'.
            '18022001280b32122e677270632e74657374696e672e4d61726b48004209'.
            '0a076172677479706522550a0c53657276657253746174757312280a0573'.
            '7461747318012001280b32192e677270632e74657374696e672e53657276'.
            '65725374617473120c0a04706f7274180220012805120d0a05636f726573'.
            '180320012805220d0a0b436f726552657175657374221d0a0c436f726552'.
            '6573706f6e7365120d0a05636f72657318012001280522060a04566f6964'.
            '22fd010a085363656e6172696f120c0a046e616d6518012001280912310a'.
            '0d636c69656e745f636f6e66696718022001280b321a2e677270632e7465'.
            '7374696e672e436c69656e74436f6e66696712130a0b6e756d5f636c6965'.
            '6e747318032001280512310a0d7365727665725f636f6e66696718042001'.
            '280b321a2e677270632e74657374696e672e536572766572436f6e666967'.
            '12130a0b6e756d5f7365727665727318052001280512160a0e7761726d75'.
            '705f7365636f6e647318062001280512190a1162656e63686d61726b5f73'.
            '65636f6e647318072001280512200a18737061776e5f6c6f63616c5f776f'.
            '726b65725f636f756e7418082001280522360a095363656e6172696f7312'.
            '290a097363656e6172696f7318012003280b32162e677270632e74657374'.
            '696e672e5363656e6172696f2284040a155363656e6172696f526573756c'.
            '7453756d6d617279120b0a03717073180120012801121b0a137170735f70'.
            '65725f7365727665725f636f7265180220012801121a0a12736572766572'.
            '5f73797374656d5f74696d6518032001280112180a107365727665725f75'.
            '7365725f74696d65180420012801121a0a12636c69656e745f7379737465'.
            '6d5f74696d6518052001280112180a10636c69656e745f757365725f7469'.
            '6d6518062001280112120a0a6c6174656e63795f35301807200128011212'.
            '0a0a6c6174656e63795f393018082001280112120a0a6c6174656e63795f'.
            '393518092001280112120a0a6c6174656e63795f3939180a200128011213'.
            '0a0b6c6174656e63795f393939180b2001280112180a107365727665725f'.
            '6370755f7573616765180c2001280112260a1e7375636365737366756c5f'.
            '72657175657374735f7065725f7365636f6e64180d2001280112220a1a66'.
            '61696c65645f72657175657374735f7065725f7365636f6e64180e200128'.
            '0112200a18636c69656e745f706f6c6c735f7065725f7265717565737418'.
            '0f2001280112200a187365727665725f706f6c6c735f7065725f72657175'.
            '65737418102001280112220a1a7365727665725f717565726965735f7065'.
            '725f6370755f73656318112001280112220a1a636c69656e745f71756572'.
            '6965735f7065725f6370755f7365631812200128012283030a0e5363656e'.
            '6172696f526573756c7412280a087363656e6172696f18012001280b3216'.
            '2e677270632e74657374696e672e5363656e6172696f122e0a096c617465'.
            '6e6369657318022001280b321b2e677270632e74657374696e672e486973'.
            '746f6772616d44617461122f0a0c636c69656e745f737461747318032003'.
            '280b32192e677270632e74657374696e672e436c69656e74537461747312'.
            '2f0a0c7365727665725f737461747318042003280b32192e677270632e74'.
            '657374696e672e536572766572537461747312140a0c7365727665725f63'.
            '6f72657318052003280512340a0773756d6d61727918062001280b32232e'.
            '677270632e74657374696e672e5363656e6172696f526573756c7453756d'.
            '6d61727912160a0e636c69656e745f737563636573731807200328081216'.
            '0a0e7365727665725f7375636365737318082003280812390a0f72657175'.
            '6573745f726573756c747318092003280b32202e677270632e7465737469'.
            '6e672e52657175657374526573756c74436f756e742a410a0a436c69656e'.
            '7454797065120f0a0b53594e435f434c49454e54100012100a0c4153594e'.
            '435f434c49454e54100112100a0c4f544845525f434c49454e5410022a5b'.
            '0a0a53657276657254797065120f0a0b53594e435f534552564552100012'.
            '100a0c4153594e435f534552564552100112180a144153594e435f47454e'.
            '455249435f534552564552100212100a0c4f544845525f53455256455210'.
            '032a720a075270635479706512090a05554e4152591000120d0a09535452'.
            '45414d494e47100112190a1553545245414d494e475f46524f4d5f434c49'.
            '454e54100212190a1553545245414d494e475f46524f4d5f534552564552'.
            '100312170a1353545245414d494e475f424f54485f574159531004620670'.
            '726f746f33'
        ));

        static::$is_initialized = true;
    }
}
