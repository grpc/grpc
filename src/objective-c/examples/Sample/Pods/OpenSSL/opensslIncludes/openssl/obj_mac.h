/* crypto/objects/obj_mac.h */

/*
 * THIS FILE IS GENERATED FROM objects.txt by objects.pl via the following
 * command: perl objects.pl objects.txt obj_mac.num obj_mac.h
 */

/* Copyright (C) 1995-1997 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#define SN_undef                        "UNDEF"
#define LN_undef                        "undefined"
#define NID_undef                       0
#define OBJ_undef                       0L

#define SN_itu_t                "ITU-T"
#define LN_itu_t                "itu-t"
#define NID_itu_t               645
#define OBJ_itu_t               0L

#define NID_ccitt               404
#define OBJ_ccitt               OBJ_itu_t

#define SN_iso          "ISO"
#define LN_iso          "iso"
#define NID_iso         181
#define OBJ_iso         1L

#define SN_joint_iso_itu_t              "JOINT-ISO-ITU-T"
#define LN_joint_iso_itu_t              "joint-iso-itu-t"
#define NID_joint_iso_itu_t             646
#define OBJ_joint_iso_itu_t             2L

#define NID_joint_iso_ccitt             393
#define OBJ_joint_iso_ccitt             OBJ_joint_iso_itu_t

#define SN_member_body          "member-body"
#define LN_member_body          "ISO Member Body"
#define NID_member_body         182
#define OBJ_member_body         OBJ_iso,2L

#define SN_identified_organization              "identified-organization"
#define NID_identified_organization             676
#define OBJ_identified_organization             OBJ_iso,3L

#define SN_hmac_md5             "HMAC-MD5"
#define LN_hmac_md5             "hmac-md5"
#define NID_hmac_md5            780
#define OBJ_hmac_md5            OBJ_identified_organization,6L,1L,5L,5L,8L,1L,1L

#define SN_hmac_sha1            "HMAC-SHA1"
#define LN_hmac_sha1            "hmac-sha1"
#define NID_hmac_sha1           781
#define OBJ_hmac_sha1           OBJ_identified_organization,6L,1L,5L,5L,8L,1L,2L

#define SN_certicom_arc         "certicom-arc"
#define NID_certicom_arc                677
#define OBJ_certicom_arc                OBJ_identified_organization,132L

#define SN_international_organizations          "international-organizations"
#define LN_international_organizations          "International Organizations"
#define NID_international_organizations         647
#define OBJ_international_organizations         OBJ_joint_iso_itu_t,23L

#define SN_wap          "wap"
#define NID_wap         678
#define OBJ_wap         OBJ_international_organizations,43L

#define SN_wap_wsg              "wap-wsg"
#define NID_wap_wsg             679
#define OBJ_wap_wsg             OBJ_wap,1L

#define SN_selected_attribute_types             "selected-attribute-types"
#define LN_selected_attribute_types             "Selected Attribute Types"
#define NID_selected_attribute_types            394
#define OBJ_selected_attribute_types            OBJ_joint_iso_itu_t,5L,1L,5L

#define SN_clearance            "clearance"
#define NID_clearance           395
#define OBJ_clearance           OBJ_selected_attribute_types,55L

#define SN_ISO_US               "ISO-US"
#define LN_ISO_US               "ISO US Member Body"
#define NID_ISO_US              183
#define OBJ_ISO_US              OBJ_member_body,840L

#define SN_X9_57                "X9-57"
#define LN_X9_57                "X9.57"
#define NID_X9_57               184
#define OBJ_X9_57               OBJ_ISO_US,10040L

#define SN_X9cm         "X9cm"
#define LN_X9cm         "X9.57 CM ?"
#define NID_X9cm                185
#define OBJ_X9cm                OBJ_X9_57,4L

#define SN_dsa          "DSA"
#define LN_dsa          "dsaEncryption"
#define NID_dsa         116
#define OBJ_dsa         OBJ_X9cm,1L

#define SN_dsaWithSHA1          "DSA-SHA1"
#define LN_dsaWithSHA1          "dsaWithSHA1"
#define NID_dsaWithSHA1         113
#define OBJ_dsaWithSHA1         OBJ_X9cm,3L

#define SN_ansi_X9_62           "ansi-X9-62"
#define LN_ansi_X9_62           "ANSI X9.62"
#define NID_ansi_X9_62          405
#define OBJ_ansi_X9_62          OBJ_ISO_US,10045L

#define OBJ_X9_62_id_fieldType          OBJ_ansi_X9_62,1L

#define SN_X9_62_prime_field            "prime-field"
#define NID_X9_62_prime_field           406
#define OBJ_X9_62_prime_field           OBJ_X9_62_id_fieldType,1L

#define SN_X9_62_characteristic_two_field               "characteristic-two-field"
#define NID_X9_62_characteristic_two_field              407
#define OBJ_X9_62_characteristic_two_field              OBJ_X9_62_id_fieldType,2L

#define SN_X9_62_id_characteristic_two_basis            "id-characteristic-two-basis"
#define NID_X9_62_id_characteristic_two_basis           680
#define OBJ_X9_62_id_characteristic_two_basis           OBJ_X9_62_characteristic_two_field,3L

#define SN_X9_62_onBasis                "onBasis"
#define NID_X9_62_onBasis               681
#define OBJ_X9_62_onBasis               OBJ_X9_62_id_characteristic_two_basis,1L

#define SN_X9_62_tpBasis                "tpBasis"
#define NID_X9_62_tpBasis               682
#define OBJ_X9_62_tpBasis               OBJ_X9_62_id_characteristic_two_basis,2L

#define SN_X9_62_ppBasis                "ppBasis"
#define NID_X9_62_ppBasis               683
#define OBJ_X9_62_ppBasis               OBJ_X9_62_id_characteristic_two_basis,3L

#define OBJ_X9_62_id_publicKeyType              OBJ_ansi_X9_62,2L

#define SN_X9_62_id_ecPublicKey         "id-ecPublicKey"
#define NID_X9_62_id_ecPublicKey                408
#define OBJ_X9_62_id_ecPublicKey                OBJ_X9_62_id_publicKeyType,1L

#define OBJ_X9_62_ellipticCurve         OBJ_ansi_X9_62,3L

#define OBJ_X9_62_c_TwoCurve            OBJ_X9_62_ellipticCurve,0L

#define SN_X9_62_c2pnb163v1             "c2pnb163v1"
#define NID_X9_62_c2pnb163v1            684
#define OBJ_X9_62_c2pnb163v1            OBJ_X9_62_c_TwoCurve,1L

#define SN_X9_62_c2pnb163v2             "c2pnb163v2"
#define NID_X9_62_c2pnb163v2            685
#define OBJ_X9_62_c2pnb163v2            OBJ_X9_62_c_TwoCurve,2L

#define SN_X9_62_c2pnb163v3             "c2pnb163v3"
#define NID_X9_62_c2pnb163v3            686
#define OBJ_X9_62_c2pnb163v3            OBJ_X9_62_c_TwoCurve,3L

#define SN_X9_62_c2pnb176v1             "c2pnb176v1"
#define NID_X9_62_c2pnb176v1            687
#define OBJ_X9_62_c2pnb176v1            OBJ_X9_62_c_TwoCurve,4L

#define SN_X9_62_c2tnb191v1             "c2tnb191v1"
#define NID_X9_62_c2tnb191v1            688
#define OBJ_X9_62_c2tnb191v1            OBJ_X9_62_c_TwoCurve,5L

#define SN_X9_62_c2tnb191v2             "c2tnb191v2"
#define NID_X9_62_c2tnb191v2            689
#define OBJ_X9_62_c2tnb191v2            OBJ_X9_62_c_TwoCurve,6L

#define SN_X9_62_c2tnb191v3             "c2tnb191v3"
#define NID_X9_62_c2tnb191v3            690
#define OBJ_X9_62_c2tnb191v3            OBJ_X9_62_c_TwoCurve,7L

#define SN_X9_62_c2onb191v4             "c2onb191v4"
#define NID_X9_62_c2onb191v4            691
#define OBJ_X9_62_c2onb191v4            OBJ_X9_62_c_TwoCurve,8L

#define SN_X9_62_c2onb191v5             "c2onb191v5"
#define NID_X9_62_c2onb191v5            692
#define OBJ_X9_62_c2onb191v5            OBJ_X9_62_c_TwoCurve,9L

#define SN_X9_62_c2pnb208w1             "c2pnb208w1"
#define NID_X9_62_c2pnb208w1            693
#define OBJ_X9_62_c2pnb208w1            OBJ_X9_62_c_TwoCurve,10L

#define SN_X9_62_c2tnb239v1             "c2tnb239v1"
#define NID_X9_62_c2tnb239v1            694
#define OBJ_X9_62_c2tnb239v1            OBJ_X9_62_c_TwoCurve,11L

#define SN_X9_62_c2tnb239v2             "c2tnb239v2"
#define NID_X9_62_c2tnb239v2            695
#define OBJ_X9_62_c2tnb239v2            OBJ_X9_62_c_TwoCurve,12L

#define SN_X9_62_c2tnb239v3             "c2tnb239v3"
#define NID_X9_62_c2tnb239v3            696
#define OBJ_X9_62_c2tnb239v3            OBJ_X9_62_c_TwoCurve,13L

#define SN_X9_62_c2onb239v4             "c2onb239v4"
#define NID_X9_62_c2onb239v4            697
#define OBJ_X9_62_c2onb239v4            OBJ_X9_62_c_TwoCurve,14L

#define SN_X9_62_c2onb239v5             "c2onb239v5"
#define NID_X9_62_c2onb239v5            698
#define OBJ_X9_62_c2onb239v5            OBJ_X9_62_c_TwoCurve,15L

#define SN_X9_62_c2pnb272w1             "c2pnb272w1"
#define NID_X9_62_c2pnb272w1            699
#define OBJ_X9_62_c2pnb272w1            OBJ_X9_62_c_TwoCurve,16L

#define SN_X9_62_c2pnb304w1             "c2pnb304w1"
#define NID_X9_62_c2pnb304w1            700
#define OBJ_X9_62_c2pnb304w1            OBJ_X9_62_c_TwoCurve,17L

#define SN_X9_62_c2tnb359v1             "c2tnb359v1"
#define NID_X9_62_c2tnb359v1            701
#define OBJ_X9_62_c2tnb359v1            OBJ_X9_62_c_TwoCurve,18L

#define SN_X9_62_c2pnb368w1             "c2pnb368w1"
#define NID_X9_62_c2pnb368w1            702
#define OBJ_X9_62_c2pnb368w1            OBJ_X9_62_c_TwoCurve,19L

#define SN_X9_62_c2tnb431r1             "c2tnb431r1"
#define NID_X9_62_c2tnb431r1            703
#define OBJ_X9_62_c2tnb431r1            OBJ_X9_62_c_TwoCurve,20L

#define OBJ_X9_62_primeCurve            OBJ_X9_62_ellipticCurve,1L

#define SN_X9_62_prime192v1             "prime192v1"
#define NID_X9_62_prime192v1            409
#define OBJ_X9_62_prime192v1            OBJ_X9_62_primeCurve,1L

#define SN_X9_62_prime192v2             "prime192v2"
#define NID_X9_62_prime192v2            410
#define OBJ_X9_62_prime192v2            OBJ_X9_62_primeCurve,2L

#define SN_X9_62_prime192v3             "prime192v3"
#define NID_X9_62_prime192v3            411
#define OBJ_X9_62_prime192v3            OBJ_X9_62_primeCurve,3L

#define SN_X9_62_prime239v1             "prime239v1"
#define NID_X9_62_prime239v1            412
#define OBJ_X9_62_prime239v1            OBJ_X9_62_primeCurve,4L

#define SN_X9_62_prime239v2             "prime239v2"
#define NID_X9_62_prime239v2            413
#define OBJ_X9_62_prime239v2            OBJ_X9_62_primeCurve,5L

#define SN_X9_62_prime239v3             "prime239v3"
#define NID_X9_62_prime239v3            414
#define OBJ_X9_62_prime239v3            OBJ_X9_62_primeCurve,6L

#define SN_X9_62_prime256v1             "prime256v1"
#define NID_X9_62_prime256v1            415
#define OBJ_X9_62_prime256v1            OBJ_X9_62_primeCurve,7L

#define OBJ_X9_62_id_ecSigType          OBJ_ansi_X9_62,4L

#define SN_ecdsa_with_SHA1              "ecdsa-with-SHA1"
#define NID_ecdsa_with_SHA1             416
#define OBJ_ecdsa_with_SHA1             OBJ_X9_62_id_ecSigType,1L

#define SN_ecdsa_with_Recommended               "ecdsa-with-Recommended"
#define NID_ecdsa_with_Recommended              791
#define OBJ_ecdsa_with_Recommended              OBJ_X9_62_id_ecSigType,2L

#define SN_ecdsa_with_Specified         "ecdsa-with-Specified"
#define NID_ecdsa_with_Specified                792
#define OBJ_ecdsa_with_Specified                OBJ_X9_62_id_ecSigType,3L

#define SN_ecdsa_with_SHA224            "ecdsa-with-SHA224"
#define NID_ecdsa_with_SHA224           793
#define OBJ_ecdsa_with_SHA224           OBJ_ecdsa_with_Specified,1L

#define SN_ecdsa_with_SHA256            "ecdsa-with-SHA256"
#define NID_ecdsa_with_SHA256           794
#define OBJ_ecdsa_with_SHA256           OBJ_ecdsa_with_Specified,2L

#define SN_ecdsa_with_SHA384            "ecdsa-with-SHA384"
#define NID_ecdsa_with_SHA384           795
#define OBJ_ecdsa_with_SHA384           OBJ_ecdsa_with_Specified,3L

#define SN_ecdsa_with_SHA512            "ecdsa-with-SHA512"
#define NID_ecdsa_with_SHA512           796
#define OBJ_ecdsa_with_SHA512           OBJ_ecdsa_with_Specified,4L

#define OBJ_secg_ellipticCurve          OBJ_certicom_arc,0L

#define SN_secp112r1            "secp112r1"
#define NID_secp112r1           704
#define OBJ_secp112r1           OBJ_secg_ellipticCurve,6L

#define SN_secp112r2            "secp112r2"
#define NID_secp112r2           705
#define OBJ_secp112r2           OBJ_secg_ellipticCurve,7L

#define SN_secp128r1            "secp128r1"
#define NID_secp128r1           706
#define OBJ_secp128r1           OBJ_secg_ellipticCurve,28L

#define SN_secp128r2            "secp128r2"
#define NID_secp128r2           707
#define OBJ_secp128r2           OBJ_secg_ellipticCurve,29L

#define SN_secp160k1            "secp160k1"
#define NID_secp160k1           708
#define OBJ_secp160k1           OBJ_secg_ellipticCurve,9L

#define SN_secp160r1            "secp160r1"
#define NID_secp160r1           709
#define OBJ_secp160r1           OBJ_secg_ellipticCurve,8L

#define SN_secp160r2            "secp160r2"
#define NID_secp160r2           710
#define OBJ_secp160r2           OBJ_secg_ellipticCurve,30L

#define SN_secp192k1            "secp192k1"
#define NID_secp192k1           711
#define OBJ_secp192k1           OBJ_secg_ellipticCurve,31L

#define SN_secp224k1            "secp224k1"
#define NID_secp224k1           712
#define OBJ_secp224k1           OBJ_secg_ellipticCurve,32L

#define SN_secp224r1            "secp224r1"
#define NID_secp224r1           713
#define OBJ_secp224r1           OBJ_secg_ellipticCurve,33L

#define SN_secp256k1            "secp256k1"
#define NID_secp256k1           714
#define OBJ_secp256k1           OBJ_secg_ellipticCurve,10L

#define SN_secp384r1            "secp384r1"
#define NID_secp384r1           715
#define OBJ_secp384r1           OBJ_secg_ellipticCurve,34L

#define SN_secp521r1            "secp521r1"
#define NID_secp521r1           716
#define OBJ_secp521r1           OBJ_secg_ellipticCurve,35L

#define SN_sect113r1            "sect113r1"
#define NID_sect113r1           717
#define OBJ_sect113r1           OBJ_secg_ellipticCurve,4L

#define SN_sect113r2            "sect113r2"
#define NID_sect113r2           718
#define OBJ_sect113r2           OBJ_secg_ellipticCurve,5L

#define SN_sect131r1            "sect131r1"
#define NID_sect131r1           719
#define OBJ_sect131r1           OBJ_secg_ellipticCurve,22L

#define SN_sect131r2            "sect131r2"
#define NID_sect131r2           720
#define OBJ_sect131r2           OBJ_secg_ellipticCurve,23L

#define SN_sect163k1            "sect163k1"
#define NID_sect163k1           721
#define OBJ_sect163k1           OBJ_secg_ellipticCurve,1L

#define SN_sect163r1            "sect163r1"
#define NID_sect163r1           722
#define OBJ_sect163r1           OBJ_secg_ellipticCurve,2L

#define SN_sect163r2            "sect163r2"
#define NID_sect163r2           723
#define OBJ_sect163r2           OBJ_secg_ellipticCurve,15L

#define SN_sect193r1            "sect193r1"
#define NID_sect193r1           724
#define OBJ_sect193r1           OBJ_secg_ellipticCurve,24L

#define SN_sect193r2            "sect193r2"
#define NID_sect193r2           725
#define OBJ_sect193r2           OBJ_secg_ellipticCurve,25L

#define SN_sect233k1            "sect233k1"
#define NID_sect233k1           726
#define OBJ_sect233k1           OBJ_secg_ellipticCurve,26L

#define SN_sect233r1            "sect233r1"
#define NID_sect233r1           727
#define OBJ_sect233r1           OBJ_secg_ellipticCurve,27L

#define SN_sect239k1            "sect239k1"
#define NID_sect239k1           728
#define OBJ_sect239k1           OBJ_secg_ellipticCurve,3L

#define SN_sect283k1            "sect283k1"
#define NID_sect283k1           729
#define OBJ_sect283k1           OBJ_secg_ellipticCurve,16L

#define SN_sect283r1            "sect283r1"
#define NID_sect283r1           730
#define OBJ_sect283r1           OBJ_secg_ellipticCurve,17L

#define SN_sect409k1            "sect409k1"
#define NID_sect409k1           731
#define OBJ_sect409k1           OBJ_secg_ellipticCurve,36L

#define SN_sect409r1            "sect409r1"
#define NID_sect409r1           732
#define OBJ_sect409r1           OBJ_secg_ellipticCurve,37L

#define SN_sect571k1            "sect571k1"
#define NID_sect571k1           733
#define OBJ_sect571k1           OBJ_secg_ellipticCurve,38L

#define SN_sect571r1            "sect571r1"
#define NID_sect571r1           734
#define OBJ_sect571r1           OBJ_secg_ellipticCurve,39L

#define OBJ_wap_wsg_idm_ecid            OBJ_wap_wsg,4L

#define SN_wap_wsg_idm_ecid_wtls1               "wap-wsg-idm-ecid-wtls1"
#define NID_wap_wsg_idm_ecid_wtls1              735
#define OBJ_wap_wsg_idm_ecid_wtls1              OBJ_wap_wsg_idm_ecid,1L

#define SN_wap_wsg_idm_ecid_wtls3               "wap-wsg-idm-ecid-wtls3"
#define NID_wap_wsg_idm_ecid_wtls3              736
#define OBJ_wap_wsg_idm_ecid_wtls3              OBJ_wap_wsg_idm_ecid,3L

#define SN_wap_wsg_idm_ecid_wtls4               "wap-wsg-idm-ecid-wtls4"
#define NID_wap_wsg_idm_ecid_wtls4              737
#define OBJ_wap_wsg_idm_ecid_wtls4              OBJ_wap_wsg_idm_ecid,4L

#define SN_wap_wsg_idm_ecid_wtls5               "wap-wsg-idm-ecid-wtls5"
#define NID_wap_wsg_idm_ecid_wtls5              738
#define OBJ_wap_wsg_idm_ecid_wtls5              OBJ_wap_wsg_idm_ecid,5L

#define SN_wap_wsg_idm_ecid_wtls6               "wap-wsg-idm-ecid-wtls6"
#define NID_wap_wsg_idm_ecid_wtls6              739
#define OBJ_wap_wsg_idm_ecid_wtls6              OBJ_wap_wsg_idm_ecid,6L

#define SN_wap_wsg_idm_ecid_wtls7               "wap-wsg-idm-ecid-wtls7"
#define NID_wap_wsg_idm_ecid_wtls7              740
#define OBJ_wap_wsg_idm_ecid_wtls7              OBJ_wap_wsg_idm_ecid,7L

#define SN_wap_wsg_idm_ecid_wtls8               "wap-wsg-idm-ecid-wtls8"
#define NID_wap_wsg_idm_ecid_wtls8              741
#define OBJ_wap_wsg_idm_ecid_wtls8              OBJ_wap_wsg_idm_ecid,8L

#define SN_wap_wsg_idm_ecid_wtls9               "wap-wsg-idm-ecid-wtls9"
#define NID_wap_wsg_idm_ecid_wtls9              742
#define OBJ_wap_wsg_idm_ecid_wtls9              OBJ_wap_wsg_idm_ecid,9L

#define SN_wap_wsg_idm_ecid_wtls10              "wap-wsg-idm-ecid-wtls10"
#define NID_wap_wsg_idm_ecid_wtls10             743
#define OBJ_wap_wsg_idm_ecid_wtls10             OBJ_wap_wsg_idm_ecid,10L

#define SN_wap_wsg_idm_ecid_wtls11              "wap-wsg-idm-ecid-wtls11"
#define NID_wap_wsg_idm_ecid_wtls11             744
#define OBJ_wap_wsg_idm_ecid_wtls11             OBJ_wap_wsg_idm_ecid,11L

#define SN_wap_wsg_idm_ecid_wtls12              "wap-wsg-idm-ecid-wtls12"
#define NID_wap_wsg_idm_ecid_wtls12             745
#define OBJ_wap_wsg_idm_ecid_wtls12             OBJ_wap_wsg_idm_ecid,12L

#define SN_cast5_cbc            "CAST5-CBC"
#define LN_cast5_cbc            "cast5-cbc"
#define NID_cast5_cbc           108
#define OBJ_cast5_cbc           OBJ_ISO_US,113533L,7L,66L,10L

#define SN_cast5_ecb            "CAST5-ECB"
#define LN_cast5_ecb            "cast5-ecb"
#define NID_cast5_ecb           109

#define SN_cast5_cfb64          "CAST5-CFB"
#define LN_cast5_cfb64          "cast5-cfb"
#define NID_cast5_cfb64         110

#define SN_cast5_ofb64          "CAST5-OFB"
#define LN_cast5_ofb64          "cast5-ofb"
#define NID_cast5_ofb64         111

#define LN_pbeWithMD5AndCast5_CBC               "pbeWithMD5AndCast5CBC"
#define NID_pbeWithMD5AndCast5_CBC              112
#define OBJ_pbeWithMD5AndCast5_CBC              OBJ_ISO_US,113533L,7L,66L,12L

#define SN_id_PasswordBasedMAC          "id-PasswordBasedMAC"
#define LN_id_PasswordBasedMAC          "password based MAC"
#define NID_id_PasswordBasedMAC         782
#define OBJ_id_PasswordBasedMAC         OBJ_ISO_US,113533L,7L,66L,13L

#define SN_id_DHBasedMac                "id-DHBasedMac"
#define LN_id_DHBasedMac                "Diffie-Hellman based MAC"
#define NID_id_DHBasedMac               783
#define OBJ_id_DHBasedMac               OBJ_ISO_US,113533L,7L,66L,30L

#define SN_rsadsi               "rsadsi"
#define LN_rsadsi               "RSA Data Security, Inc."
#define NID_rsadsi              1
#define OBJ_rsadsi              OBJ_ISO_US,113549L

#define SN_pkcs         "pkcs"
#define LN_pkcs         "RSA Data Security, Inc. PKCS"
#define NID_pkcs                2
#define OBJ_pkcs                OBJ_rsadsi,1L

#define SN_pkcs1                "pkcs1"
#define NID_pkcs1               186
#define OBJ_pkcs1               OBJ_pkcs,1L

#define LN_rsaEncryption                "rsaEncryption"
#define NID_rsaEncryption               6
#define OBJ_rsaEncryption               OBJ_pkcs1,1L

#define SN_md2WithRSAEncryption         "RSA-MD2"
#define LN_md2WithRSAEncryption         "md2WithRSAEncryption"
#define NID_md2WithRSAEncryption                7
#define OBJ_md2WithRSAEncryption                OBJ_pkcs1,2L

#define SN_md4WithRSAEncryption         "RSA-MD4"
#define LN_md4WithRSAEncryption         "md4WithRSAEncryption"
#define NID_md4WithRSAEncryption                396
#define OBJ_md4WithRSAEncryption                OBJ_pkcs1,3L

#define SN_md5WithRSAEncryption         "RSA-MD5"
#define LN_md5WithRSAEncryption         "md5WithRSAEncryption"
#define NID_md5WithRSAEncryption                8
#define OBJ_md5WithRSAEncryption                OBJ_pkcs1,4L

#define SN_sha1WithRSAEncryption                "RSA-SHA1"
#define LN_sha1WithRSAEncryption                "sha1WithRSAEncryption"
#define NID_sha1WithRSAEncryption               65
#define OBJ_sha1WithRSAEncryption               OBJ_pkcs1,5L

#define SN_rsaesOaep            "RSAES-OAEP"
#define LN_rsaesOaep            "rsaesOaep"
#define NID_rsaesOaep           919
#define OBJ_rsaesOaep           OBJ_pkcs1,7L

#define SN_mgf1         "MGF1"
#define LN_mgf1         "mgf1"
#define NID_mgf1                911
#define OBJ_mgf1                OBJ_pkcs1,8L

#define SN_pSpecified           "PSPECIFIED"
#define LN_pSpecified           "pSpecified"
#define NID_pSpecified          935
#define OBJ_pSpecified          OBJ_pkcs1,9L

#define SN_rsassaPss            "RSASSA-PSS"
#define LN_rsassaPss            "rsassaPss"
#define NID_rsassaPss           912
#define OBJ_rsassaPss           OBJ_pkcs1,10L

#define SN_sha256WithRSAEncryption              "RSA-SHA256"
#define LN_sha256WithRSAEncryption              "sha256WithRSAEncryption"
#define NID_sha256WithRSAEncryption             668
#define OBJ_sha256WithRSAEncryption             OBJ_pkcs1,11L

#define SN_sha384WithRSAEncryption              "RSA-SHA384"
#define LN_sha384WithRSAEncryption              "sha384WithRSAEncryption"
#define NID_sha384WithRSAEncryption             669
#define OBJ_sha384WithRSAEncryption             OBJ_pkcs1,12L

#define SN_sha512WithRSAEncryption              "RSA-SHA512"
#define LN_sha512WithRSAEncryption              "sha512WithRSAEncryption"
#define NID_sha512WithRSAEncryption             670
#define OBJ_sha512WithRSAEncryption             OBJ_pkcs1,13L

#define SN_sha224WithRSAEncryption              "RSA-SHA224"
#define LN_sha224WithRSAEncryption              "sha224WithRSAEncryption"
#define NID_sha224WithRSAEncryption             671
#define OBJ_sha224WithRSAEncryption             OBJ_pkcs1,14L

#define SN_pkcs3                "pkcs3"
#define NID_pkcs3               27
#define OBJ_pkcs3               OBJ_pkcs,3L

#define LN_dhKeyAgreement               "dhKeyAgreement"
#define NID_dhKeyAgreement              28
#define OBJ_dhKeyAgreement              OBJ_pkcs3,1L

#define SN_pkcs5                "pkcs5"
#define NID_pkcs5               187
#define OBJ_pkcs5               OBJ_pkcs,5L

#define SN_pbeWithMD2AndDES_CBC         "PBE-MD2-DES"
#define LN_pbeWithMD2AndDES_CBC         "pbeWithMD2AndDES-CBC"
#define NID_pbeWithMD2AndDES_CBC                9
#define OBJ_pbeWithMD2AndDES_CBC                OBJ_pkcs5,1L

#define SN_pbeWithMD5AndDES_CBC         "PBE-MD5-DES"
#define LN_pbeWithMD5AndDES_CBC         "pbeWithMD5AndDES-CBC"
#define NID_pbeWithMD5AndDES_CBC                10
#define OBJ_pbeWithMD5AndDES_CBC                OBJ_pkcs5,3L

#define SN_pbeWithMD2AndRC2_CBC         "PBE-MD2-RC2-64"
#define LN_pbeWithMD2AndRC2_CBC         "pbeWithMD2AndRC2-CBC"
#define NID_pbeWithMD2AndRC2_CBC                168
#define OBJ_pbeWithMD2AndRC2_CBC                OBJ_pkcs5,4L

#define SN_pbeWithMD5AndRC2_CBC         "PBE-MD5-RC2-64"
#define LN_pbeWithMD5AndRC2_CBC         "pbeWithMD5AndRC2-CBC"
#define NID_pbeWithMD5AndRC2_CBC                169
#define OBJ_pbeWithMD5AndRC2_CBC                OBJ_pkcs5,6L

#define SN_pbeWithSHA1AndDES_CBC                "PBE-SHA1-DES"
#define LN_pbeWithSHA1AndDES_CBC                "pbeWithSHA1AndDES-CBC"
#define NID_pbeWithSHA1AndDES_CBC               170
#define OBJ_pbeWithSHA1AndDES_CBC               OBJ_pkcs5,10L

#define SN_pbeWithSHA1AndRC2_CBC                "PBE-SHA1-RC2-64"
#define LN_pbeWithSHA1AndRC2_CBC                "pbeWithSHA1AndRC2-CBC"
#define NID_pbeWithSHA1AndRC2_CBC               68
#define OBJ_pbeWithSHA1AndRC2_CBC               OBJ_pkcs5,11L

#define LN_id_pbkdf2            "PBKDF2"
#define NID_id_pbkdf2           69
#define OBJ_id_pbkdf2           OBJ_pkcs5,12L

#define LN_pbes2                "PBES2"
#define NID_pbes2               161
#define OBJ_pbes2               OBJ_pkcs5,13L

#define LN_pbmac1               "PBMAC1"
#define NID_pbmac1              162
#define OBJ_pbmac1              OBJ_pkcs5,14L

#define SN_pkcs7                "pkcs7"
#define NID_pkcs7               20
#define OBJ_pkcs7               OBJ_pkcs,7L

#define LN_pkcs7_data           "pkcs7-data"
#define NID_pkcs7_data          21
#define OBJ_pkcs7_data          OBJ_pkcs7,1L

#define LN_pkcs7_signed         "pkcs7-signedData"
#define NID_pkcs7_signed                22
#define OBJ_pkcs7_signed                OBJ_pkcs7,2L

#define LN_pkcs7_enveloped              "pkcs7-envelopedData"
#define NID_pkcs7_enveloped             23
#define OBJ_pkcs7_enveloped             OBJ_pkcs7,3L

#define LN_pkcs7_signedAndEnveloped             "pkcs7-signedAndEnvelopedData"
#define NID_pkcs7_signedAndEnveloped            24
#define OBJ_pkcs7_signedAndEnveloped            OBJ_pkcs7,4L

#define LN_pkcs7_digest         "pkcs7-digestData"
#define NID_pkcs7_digest                25
#define OBJ_pkcs7_digest                OBJ_pkcs7,5L

#define LN_pkcs7_encrypted              "pkcs7-encryptedData"
#define NID_pkcs7_encrypted             26
#define OBJ_pkcs7_encrypted             OBJ_pkcs7,6L

#define SN_pkcs9                "pkcs9"
#define NID_pkcs9               47
#define OBJ_pkcs9               OBJ_pkcs,9L

#define LN_pkcs9_emailAddress           "emailAddress"
#define NID_pkcs9_emailAddress          48
#define OBJ_pkcs9_emailAddress          OBJ_pkcs9,1L

#define LN_pkcs9_unstructuredName               "unstructuredName"
#define NID_pkcs9_unstructuredName              49
#define OBJ_pkcs9_unstructuredName              OBJ_pkcs9,2L

#define LN_pkcs9_contentType            "contentType"
#define NID_pkcs9_contentType           50
#define OBJ_pkcs9_contentType           OBJ_pkcs9,3L

#define LN_pkcs9_messageDigest          "messageDigest"
#define NID_pkcs9_messageDigest         51
#define OBJ_pkcs9_messageDigest         OBJ_pkcs9,4L

#define LN_pkcs9_signingTime            "signingTime"
#define NID_pkcs9_signingTime           52
#define OBJ_pkcs9_signingTime           OBJ_pkcs9,5L

#define LN_pkcs9_countersignature               "countersignature"
#define NID_pkcs9_countersignature              53
#define OBJ_pkcs9_countersignature              OBJ_pkcs9,6L

#define LN_pkcs9_challengePassword              "challengePassword"
#define NID_pkcs9_challengePassword             54
#define OBJ_pkcs9_challengePassword             OBJ_pkcs9,7L

#define LN_pkcs9_unstructuredAddress            "unstructuredAddress"
#define NID_pkcs9_unstructuredAddress           55
#define OBJ_pkcs9_unstructuredAddress           OBJ_pkcs9,8L

#define LN_pkcs9_extCertAttributes              "extendedCertificateAttributes"
#define NID_pkcs9_extCertAttributes             56
#define OBJ_pkcs9_extCertAttributes             OBJ_pkcs9,9L

#define SN_ext_req              "extReq"
#define LN_ext_req              "Extension Request"
#define NID_ext_req             172
#define OBJ_ext_req             OBJ_pkcs9,14L

#define SN_SMIMECapabilities            "SMIME-CAPS"
#define LN_SMIMECapabilities            "S/MIME Capabilities"
#define NID_SMIMECapabilities           167
#define OBJ_SMIMECapabilities           OBJ_pkcs9,15L

#define SN_SMIME                "SMIME"
#define LN_SMIME                "S/MIME"
#define NID_SMIME               188
#define OBJ_SMIME               OBJ_pkcs9,16L

#define SN_id_smime_mod         "id-smime-mod"
#define NID_id_smime_mod                189
#define OBJ_id_smime_mod                OBJ_SMIME,0L

#define SN_id_smime_ct          "id-smime-ct"
#define NID_id_smime_ct         190
#define OBJ_id_smime_ct         OBJ_SMIME,1L

#define SN_id_smime_aa          "id-smime-aa"
#define NID_id_smime_aa         191
#define OBJ_id_smime_aa         OBJ_SMIME,2L

#define SN_id_smime_alg         "id-smime-alg"
#define NID_id_smime_alg                192
#define OBJ_id_smime_alg                OBJ_SMIME,3L

#define SN_id_smime_cd          "id-smime-cd"
#define NID_id_smime_cd         193
#define OBJ_id_smime_cd         OBJ_SMIME,4L

#define SN_id_smime_spq         "id-smime-spq"
#define NID_id_smime_spq                194
#define OBJ_id_smime_spq                OBJ_SMIME,5L

#define SN_id_smime_cti         "id-smime-cti"
#define NID_id_smime_cti                195
#define OBJ_id_smime_cti                OBJ_SMIME,6L

#define SN_id_smime_mod_cms             "id-smime-mod-cms"
#define NID_id_smime_mod_cms            196
#define OBJ_id_smime_mod_cms            OBJ_id_smime_mod,1L

#define SN_id_smime_mod_ess             "id-smime-mod-ess"
#define NID_id_smime_mod_ess            197
#define OBJ_id_smime_mod_ess            OBJ_id_smime_mod,2L

#define SN_id_smime_mod_oid             "id-smime-mod-oid"
#define NID_id_smime_mod_oid            198
#define OBJ_id_smime_mod_oid            OBJ_id_smime_mod,3L

#define SN_id_smime_mod_msg_v3          "id-smime-mod-msg-v3"
#define NID_id_smime_mod_msg_v3         199
#define OBJ_id_smime_mod_msg_v3         OBJ_id_smime_mod,4L

#define SN_id_smime_mod_ets_eSignature_88               "id-smime-mod-ets-eSignature-88"
#define NID_id_smime_mod_ets_eSignature_88              200
#define OBJ_id_smime_mod_ets_eSignature_88              OBJ_id_smime_mod,5L

#define SN_id_smime_mod_ets_eSignature_97               "id-smime-mod-ets-eSignature-97"
#define NID_id_smime_mod_ets_eSignature_97              201
#define OBJ_id_smime_mod_ets_eSignature_97              OBJ_id_smime_mod,6L

#define SN_id_smime_mod_ets_eSigPolicy_88               "id-smime-mod-ets-eSigPolicy-88"
#define NID_id_smime_mod_ets_eSigPolicy_88              202
#define OBJ_id_smime_mod_ets_eSigPolicy_88              OBJ_id_smime_mod,7L

#define SN_id_smime_mod_ets_eSigPolicy_97               "id-smime-mod-ets-eSigPolicy-97"
#define NID_id_smime_mod_ets_eSigPolicy_97              203
#define OBJ_id_smime_mod_ets_eSigPolicy_97              OBJ_id_smime_mod,8L

#define SN_id_smime_ct_receipt          "id-smime-ct-receipt"
#define NID_id_smime_ct_receipt         204
#define OBJ_id_smime_ct_receipt         OBJ_id_smime_ct,1L

#define SN_id_smime_ct_authData         "id-smime-ct-authData"
#define NID_id_smime_ct_authData                205
#define OBJ_id_smime_ct_authData                OBJ_id_smime_ct,2L

#define SN_id_smime_ct_publishCert              "id-smime-ct-publishCert"
#define NID_id_smime_ct_publishCert             206
#define OBJ_id_smime_ct_publishCert             OBJ_id_smime_ct,3L

#define SN_id_smime_ct_TSTInfo          "id-smime-ct-TSTInfo"
#define NID_id_smime_ct_TSTInfo         207
#define OBJ_id_smime_ct_TSTInfo         OBJ_id_smime_ct,4L

#define SN_id_smime_ct_TDTInfo          "id-smime-ct-TDTInfo"
#define NID_id_smime_ct_TDTInfo         208
#define OBJ_id_smime_ct_TDTInfo         OBJ_id_smime_ct,5L

#define SN_id_smime_ct_contentInfo              "id-smime-ct-contentInfo"
#define NID_id_smime_ct_contentInfo             209
#define OBJ_id_smime_ct_contentInfo             OBJ_id_smime_ct,6L

#define SN_id_smime_ct_DVCSRequestData          "id-smime-ct-DVCSRequestData"
#define NID_id_smime_ct_DVCSRequestData         210
#define OBJ_id_smime_ct_DVCSRequestData         OBJ_id_smime_ct,7L

#define SN_id_smime_ct_DVCSResponseData         "id-smime-ct-DVCSResponseData"
#define NID_id_smime_ct_DVCSResponseData                211
#define OBJ_id_smime_ct_DVCSResponseData                OBJ_id_smime_ct,8L

#define SN_id_smime_ct_compressedData           "id-smime-ct-compressedData"
#define NID_id_smime_ct_compressedData          786
#define OBJ_id_smime_ct_compressedData          OBJ_id_smime_ct,9L

#define SN_id_ct_asciiTextWithCRLF              "id-ct-asciiTextWithCRLF"
#define NID_id_ct_asciiTextWithCRLF             787
#define OBJ_id_ct_asciiTextWithCRLF             OBJ_id_smime_ct,27L

#define SN_id_smime_aa_receiptRequest           "id-smime-aa-receiptRequest"
#define NID_id_smime_aa_receiptRequest          212
#define OBJ_id_smime_aa_receiptRequest          OBJ_id_smime_aa,1L

#define SN_id_smime_aa_securityLabel            "id-smime-aa-securityLabel"
#define NID_id_smime_aa_securityLabel           213
#define OBJ_id_smime_aa_securityLabel           OBJ_id_smime_aa,2L

#define SN_id_smime_aa_mlExpandHistory          "id-smime-aa-mlExpandHistory"
#define NID_id_smime_aa_mlExpandHistory         214
#define OBJ_id_smime_aa_mlExpandHistory         OBJ_id_smime_aa,3L

#define SN_id_smime_aa_contentHint              "id-smime-aa-contentHint"
#define NID_id_smime_aa_contentHint             215
#define OBJ_id_smime_aa_contentHint             OBJ_id_smime_aa,4L

#define SN_id_smime_aa_msgSigDigest             "id-smime-aa-msgSigDigest"
#define NID_id_smime_aa_msgSigDigest            216
#define OBJ_id_smime_aa_msgSigDigest            OBJ_id_smime_aa,5L

#define SN_id_smime_aa_encapContentType         "id-smime-aa-encapContentType"
#define NID_id_smime_aa_encapContentType                217
#define OBJ_id_smime_aa_encapContentType                OBJ_id_smime_aa,6L

#define SN_id_smime_aa_contentIdentifier                "id-smime-aa-contentIdentifier"
#define NID_id_smime_aa_contentIdentifier               218
#define OBJ_id_smime_aa_contentIdentifier               OBJ_id_smime_aa,7L

#define SN_id_smime_aa_macValue         "id-smime-aa-macValue"
#define NID_id_smime_aa_macValue                219
#define OBJ_id_smime_aa_macValue                OBJ_id_smime_aa,8L

#define SN_id_smime_aa_equivalentLabels         "id-smime-aa-equivalentLabels"
#define NID_id_smime_aa_equivalentLabels                220
#define OBJ_id_smime_aa_equivalentLabels                OBJ_id_smime_aa,9L

#define SN_id_smime_aa_contentReference         "id-smime-aa-contentReference"
#define NID_id_smime_aa_contentReference                221
#define OBJ_id_smime_aa_contentReference                OBJ_id_smime_aa,10L

#define SN_id_smime_aa_encrypKeyPref            "id-smime-aa-encrypKeyPref"
#define NID_id_smime_aa_encrypKeyPref           222
#define OBJ_id_smime_aa_encrypKeyPref           OBJ_id_smime_aa,11L

#define SN_id_smime_aa_signingCertificate               "id-smime-aa-signingCertificate"
#define NID_id_smime_aa_signingCertificate              223
#define OBJ_id_smime_aa_signingCertificate              OBJ_id_smime_aa,12L

#define SN_id_smime_aa_smimeEncryptCerts                "id-smime-aa-smimeEncryptCerts"
#define NID_id_smime_aa_smimeEncryptCerts               224
#define OBJ_id_smime_aa_smimeEncryptCerts               OBJ_id_smime_aa,13L

#define SN_id_smime_aa_timeStampToken           "id-smime-aa-timeStampToken"
#define NID_id_smime_aa_timeStampToken          225
#define OBJ_id_smime_aa_timeStampToken          OBJ_id_smime_aa,14L

#define SN_id_smime_aa_ets_sigPolicyId          "id-smime-aa-ets-sigPolicyId"
#define NID_id_smime_aa_ets_sigPolicyId         226
#define OBJ_id_smime_aa_ets_sigPolicyId         OBJ_id_smime_aa,15L

#define SN_id_smime_aa_ets_commitmentType               "id-smime-aa-ets-commitmentType"
#define NID_id_smime_aa_ets_commitmentType              227
#define OBJ_id_smime_aa_ets_commitmentType              OBJ_id_smime_aa,16L

#define SN_id_smime_aa_ets_signerLocation               "id-smime-aa-ets-signerLocation"
#define NID_id_smime_aa_ets_signerLocation              228
#define OBJ_id_smime_aa_ets_signerLocation              OBJ_id_smime_aa,17L

#define SN_id_smime_aa_ets_signerAttr           "id-smime-aa-ets-signerAttr"
#define NID_id_smime_aa_ets_signerAttr          229
#define OBJ_id_smime_aa_ets_signerAttr          OBJ_id_smime_aa,18L

#define SN_id_smime_aa_ets_otherSigCert         "id-smime-aa-ets-otherSigCert"
#define NID_id_smime_aa_ets_otherSigCert                230
#define OBJ_id_smime_aa_ets_otherSigCert                OBJ_id_smime_aa,19L

#define SN_id_smime_aa_ets_contentTimestamp             "id-smime-aa-ets-contentTimestamp"
#define NID_id_smime_aa_ets_contentTimestamp            231
#define OBJ_id_smime_aa_ets_contentTimestamp            OBJ_id_smime_aa,20L

#define SN_id_smime_aa_ets_CertificateRefs              "id-smime-aa-ets-CertificateRefs"
#define NID_id_smime_aa_ets_CertificateRefs             232
#define OBJ_id_smime_aa_ets_CertificateRefs             OBJ_id_smime_aa,21L

#define SN_id_smime_aa_ets_RevocationRefs               "id-smime-aa-ets-RevocationRefs"
#define NID_id_smime_aa_ets_RevocationRefs              233
#define OBJ_id_smime_aa_ets_RevocationRefs              OBJ_id_smime_aa,22L

#define SN_id_smime_aa_ets_certValues           "id-smime-aa-ets-certValues"
#define NID_id_smime_aa_ets_certValues          234
#define OBJ_id_smime_aa_ets_certValues          OBJ_id_smime_aa,23L

#define SN_id_smime_aa_ets_revocationValues             "id-smime-aa-ets-revocationValues"
#define NID_id_smime_aa_ets_revocationValues            235
#define OBJ_id_smime_aa_ets_revocationValues            OBJ_id_smime_aa,24L

#define SN_id_smime_aa_ets_escTimeStamp         "id-smime-aa-ets-escTimeStamp"
#define NID_id_smime_aa_ets_escTimeStamp                236
#define OBJ_id_smime_aa_ets_escTimeStamp                OBJ_id_smime_aa,25L

#define SN_id_smime_aa_ets_certCRLTimestamp             "id-smime-aa-ets-certCRLTimestamp"
#define NID_id_smime_aa_ets_certCRLTimestamp            237
#define OBJ_id_smime_aa_ets_certCRLTimestamp            OBJ_id_smime_aa,26L

#define SN_id_smime_aa_ets_archiveTimeStamp             "id-smime-aa-ets-archiveTimeStamp"
#define NID_id_smime_aa_ets_archiveTimeStamp            238
#define OBJ_id_smime_aa_ets_archiveTimeStamp            OBJ_id_smime_aa,27L

#define SN_id_smime_aa_signatureType            "id-smime-aa-signatureType"
#define NID_id_smime_aa_signatureType           239
#define OBJ_id_smime_aa_signatureType           OBJ_id_smime_aa,28L

#define SN_id_smime_aa_dvcs_dvc         "id-smime-aa-dvcs-dvc"
#define NID_id_smime_aa_dvcs_dvc                240
#define OBJ_id_smime_aa_dvcs_dvc                OBJ_id_smime_aa,29L

#define SN_id_smime_alg_ESDHwith3DES            "id-smime-alg-ESDHwith3DES"
#define NID_id_smime_alg_ESDHwith3DES           241
#define OBJ_id_smime_alg_ESDHwith3DES           OBJ_id_smime_alg,1L

#define SN_id_smime_alg_ESDHwithRC2             "id-smime-alg-ESDHwithRC2"
#define NID_id_smime_alg_ESDHwithRC2            242
#define OBJ_id_smime_alg_ESDHwithRC2            OBJ_id_smime_alg,2L

#define SN_id_smime_alg_3DESwrap                "id-smime-alg-3DESwrap"
#define NID_id_smime_alg_3DESwrap               243
#define OBJ_id_smime_alg_3DESwrap               OBJ_id_smime_alg,3L

#define SN_id_smime_alg_RC2wrap         "id-smime-alg-RC2wrap"
#define NID_id_smime_alg_RC2wrap                244
#define OBJ_id_smime_alg_RC2wrap                OBJ_id_smime_alg,4L

#define SN_id_smime_alg_ESDH            "id-smime-alg-ESDH"
#define NID_id_smime_alg_ESDH           245
#define OBJ_id_smime_alg_ESDH           OBJ_id_smime_alg,5L

#define SN_id_smime_alg_CMS3DESwrap             "id-smime-alg-CMS3DESwrap"
#define NID_id_smime_alg_CMS3DESwrap            246
#define OBJ_id_smime_alg_CMS3DESwrap            OBJ_id_smime_alg,6L

#define SN_id_smime_alg_CMSRC2wrap              "id-smime-alg-CMSRC2wrap"
#define NID_id_smime_alg_CMSRC2wrap             247
#define OBJ_id_smime_alg_CMSRC2wrap             OBJ_id_smime_alg,7L

#define SN_id_alg_PWRI_KEK              "id-alg-PWRI-KEK"
#define NID_id_alg_PWRI_KEK             893
#define OBJ_id_alg_PWRI_KEK             OBJ_id_smime_alg,9L

#define SN_id_smime_cd_ldap             "id-smime-cd-ldap"
#define NID_id_smime_cd_ldap            248
#define OBJ_id_smime_cd_ldap            OBJ_id_smime_cd,1L

#define SN_id_smime_spq_ets_sqt_uri             "id-smime-spq-ets-sqt-uri"
#define NID_id_smime_spq_ets_sqt_uri            249
#define OBJ_id_smime_spq_ets_sqt_uri            OBJ_id_smime_spq,1L

#define SN_id_smime_spq_ets_sqt_unotice         "id-smime-spq-ets-sqt-unotice"
#define NID_id_smime_spq_ets_sqt_unotice                250
#define OBJ_id_smime_spq_ets_sqt_unotice                OBJ_id_smime_spq,2L

#define SN_id_smime_cti_ets_proofOfOrigin               "id-smime-cti-ets-proofOfOrigin"
#define NID_id_smime_cti_ets_proofOfOrigin              251
#define OBJ_id_smime_cti_ets_proofOfOrigin              OBJ_id_smime_cti,1L

#define SN_id_smime_cti_ets_proofOfReceipt              "id-smime-cti-ets-proofOfReceipt"
#define NID_id_smime_cti_ets_proofOfReceipt             252
#define OBJ_id_smime_cti_ets_proofOfReceipt             OBJ_id_smime_cti,2L

#define SN_id_smime_cti_ets_proofOfDelivery             "id-smime-cti-ets-proofOfDelivery"
#define NID_id_smime_cti_ets_proofOfDelivery            253
#define OBJ_id_smime_cti_ets_proofOfDelivery            OBJ_id_smime_cti,3L

#define SN_id_smime_cti_ets_proofOfSender               "id-smime-cti-ets-proofOfSender"
#define NID_id_smime_cti_ets_proofOfSender              254
#define OBJ_id_smime_cti_ets_proofOfSender              OBJ_id_smime_cti,4L

#define SN_id_smime_cti_ets_proofOfApproval             "id-smime-cti-ets-proofOfApproval"
#define NID_id_smime_cti_ets_proofOfApproval            255
#define OBJ_id_smime_cti_ets_proofOfApproval            OBJ_id_smime_cti,5L

#define SN_id_smime_cti_ets_proofOfCreation             "id-smime-cti-ets-proofOfCreation"
#define NID_id_smime_cti_ets_proofOfCreation            256
#define OBJ_id_smime_cti_ets_proofOfCreation            OBJ_id_smime_cti,6L

#define LN_friendlyName         "friendlyName"
#define NID_friendlyName                156
#define OBJ_friendlyName                OBJ_pkcs9,20L

#define LN_localKeyID           "localKeyID"
#define NID_localKeyID          157
#define OBJ_localKeyID          OBJ_pkcs9,21L

#define SN_ms_csp_name          "CSPName"
#define LN_ms_csp_name          "Microsoft CSP Name"
#define NID_ms_csp_name         417
#define OBJ_ms_csp_name         1L,3L,6L,1L,4L,1L,311L,17L,1L

#define SN_LocalKeySet          "LocalKeySet"
#define LN_LocalKeySet          "Microsoft Local Key set"
#define NID_LocalKeySet         856
#define OBJ_LocalKeySet         1L,3L,6L,1L,4L,1L,311L,17L,2L

#define OBJ_certTypes           OBJ_pkcs9,22L

#define LN_x509Certificate              "x509Certificate"
#define NID_x509Certificate             158
#define OBJ_x509Certificate             OBJ_certTypes,1L

#define LN_sdsiCertificate              "sdsiCertificate"
#define NID_sdsiCertificate             159
#define OBJ_sdsiCertificate             OBJ_certTypes,2L

#define OBJ_crlTypes            OBJ_pkcs9,23L

#define LN_x509Crl              "x509Crl"
#define NID_x509Crl             160
#define OBJ_x509Crl             OBJ_crlTypes,1L

#define OBJ_pkcs12              OBJ_pkcs,12L

#define OBJ_pkcs12_pbeids               OBJ_pkcs12,1L

#define SN_pbe_WithSHA1And128BitRC4             "PBE-SHA1-RC4-128"
#define LN_pbe_WithSHA1And128BitRC4             "pbeWithSHA1And128BitRC4"
#define NID_pbe_WithSHA1And128BitRC4            144
#define OBJ_pbe_WithSHA1And128BitRC4            OBJ_pkcs12_pbeids,1L

#define SN_pbe_WithSHA1And40BitRC4              "PBE-SHA1-RC4-40"
#define LN_pbe_WithSHA1And40BitRC4              "pbeWithSHA1And40BitRC4"
#define NID_pbe_WithSHA1And40BitRC4             145
#define OBJ_pbe_WithSHA1And40BitRC4             OBJ_pkcs12_pbeids,2L

#define SN_pbe_WithSHA1And3_Key_TripleDES_CBC           "PBE-SHA1-3DES"
#define LN_pbe_WithSHA1And3_Key_TripleDES_CBC           "pbeWithSHA1And3-KeyTripleDES-CBC"
#define NID_pbe_WithSHA1And3_Key_TripleDES_CBC          146
#define OBJ_pbe_WithSHA1And3_Key_TripleDES_CBC          OBJ_pkcs12_pbeids,3L

#define SN_pbe_WithSHA1And2_Key_TripleDES_CBC           "PBE-SHA1-2DES"
#define LN_pbe_WithSHA1And2_Key_TripleDES_CBC           "pbeWithSHA1And2-KeyTripleDES-CBC"
#define NID_pbe_WithSHA1And2_Key_TripleDES_CBC          147
#define OBJ_pbe_WithSHA1And2_Key_TripleDES_CBC          OBJ_pkcs12_pbeids,4L

#define SN_pbe_WithSHA1And128BitRC2_CBC         "PBE-SHA1-RC2-128"
#define LN_pbe_WithSHA1And128BitRC2_CBC         "pbeWithSHA1And128BitRC2-CBC"
#define NID_pbe_WithSHA1And128BitRC2_CBC                148
#define OBJ_pbe_WithSHA1And128BitRC2_CBC                OBJ_pkcs12_pbeids,5L

#define SN_pbe_WithSHA1And40BitRC2_CBC          "PBE-SHA1-RC2-40"
#define LN_pbe_WithSHA1And40BitRC2_CBC          "pbeWithSHA1And40BitRC2-CBC"
#define NID_pbe_WithSHA1And40BitRC2_CBC         149
#define OBJ_pbe_WithSHA1And40BitRC2_CBC         OBJ_pkcs12_pbeids,6L

#define OBJ_pkcs12_Version1             OBJ_pkcs12,10L

#define OBJ_pkcs12_BagIds               OBJ_pkcs12_Version1,1L

#define LN_keyBag               "keyBag"
#define NID_keyBag              150
#define OBJ_keyBag              OBJ_pkcs12_BagIds,1L

#define LN_pkcs8ShroudedKeyBag          "pkcs8ShroudedKeyBag"
#define NID_pkcs8ShroudedKeyBag         151
#define OBJ_pkcs8ShroudedKeyBag         OBJ_pkcs12_BagIds,2L

#define LN_certBag              "certBag"
#define NID_certBag             152
#define OBJ_certBag             OBJ_pkcs12_BagIds,3L

#define LN_crlBag               "crlBag"
#define NID_crlBag              153
#define OBJ_crlBag              OBJ_pkcs12_BagIds,4L

#define LN_secretBag            "secretBag"
#define NID_secretBag           154
#define OBJ_secretBag           OBJ_pkcs12_BagIds,5L

#define LN_safeContentsBag              "safeContentsBag"
#define NID_safeContentsBag             155
#define OBJ_safeContentsBag             OBJ_pkcs12_BagIds,6L

#define SN_md2          "MD2"
#define LN_md2          "md2"
#define NID_md2         3
#define OBJ_md2         OBJ_rsadsi,2L,2L

#define SN_md4          "MD4"
#define LN_md4          "md4"
#define NID_md4         257
#define OBJ_md4         OBJ_rsadsi,2L,4L

#define SN_md5          "MD5"
#define LN_md5          "md5"
#define NID_md5         4
#define OBJ_md5         OBJ_rsadsi,2L,5L

#define SN_md5_sha1             "MD5-SHA1"
#define LN_md5_sha1             "md5-sha1"
#define NID_md5_sha1            114

#define LN_hmacWithMD5          "hmacWithMD5"
#define NID_hmacWithMD5         797
#define OBJ_hmacWithMD5         OBJ_rsadsi,2L,6L

#define LN_hmacWithSHA1         "hmacWithSHA1"
#define NID_hmacWithSHA1                163
#define OBJ_hmacWithSHA1                OBJ_rsadsi,2L,7L

#define LN_hmacWithSHA224               "hmacWithSHA224"
#define NID_hmacWithSHA224              798
#define OBJ_hmacWithSHA224              OBJ_rsadsi,2L,8L

#define LN_hmacWithSHA256               "hmacWithSHA256"
#define NID_hmacWithSHA256              799
#define OBJ_hmacWithSHA256              OBJ_rsadsi,2L,9L

#define LN_hmacWithSHA384               "hmacWithSHA384"
#define NID_hmacWithSHA384              800
#define OBJ_hmacWithSHA384              OBJ_rsadsi,2L,10L

#define LN_hmacWithSHA512               "hmacWithSHA512"
#define NID_hmacWithSHA512              801
#define OBJ_hmacWithSHA512              OBJ_rsadsi,2L,11L

#define SN_rc2_cbc              "RC2-CBC"
#define LN_rc2_cbc              "rc2-cbc"
#define NID_rc2_cbc             37
#define OBJ_rc2_cbc             OBJ_rsadsi,3L,2L

#define SN_rc2_ecb              "RC2-ECB"
#define LN_rc2_ecb              "rc2-ecb"
#define NID_rc2_ecb             38

#define SN_rc2_cfb64            "RC2-CFB"
#define LN_rc2_cfb64            "rc2-cfb"
#define NID_rc2_cfb64           39

#define SN_rc2_ofb64            "RC2-OFB"
#define LN_rc2_ofb64            "rc2-ofb"
#define NID_rc2_ofb64           40

#define SN_rc2_40_cbc           "RC2-40-CBC"
#define LN_rc2_40_cbc           "rc2-40-cbc"
#define NID_rc2_40_cbc          98

#define SN_rc2_64_cbc           "RC2-64-CBC"
#define LN_rc2_64_cbc           "rc2-64-cbc"
#define NID_rc2_64_cbc          166

#define SN_rc4          "RC4"
#define LN_rc4          "rc4"
#define NID_rc4         5
#define OBJ_rc4         OBJ_rsadsi,3L,4L

#define SN_rc4_40               "RC4-40"
#define LN_rc4_40               "rc4-40"
#define NID_rc4_40              97

#define SN_des_ede3_cbc         "DES-EDE3-CBC"
#define LN_des_ede3_cbc         "des-ede3-cbc"
#define NID_des_ede3_cbc                44
#define OBJ_des_ede3_cbc                OBJ_rsadsi,3L,7L

#define SN_rc5_cbc              "RC5-CBC"
#define LN_rc5_cbc              "rc5-cbc"
#define NID_rc5_cbc             120
#define OBJ_rc5_cbc             OBJ_rsadsi,3L,8L

#define SN_rc5_ecb              "RC5-ECB"
#define LN_rc5_ecb              "rc5-ecb"
#define NID_rc5_ecb             121

#define SN_rc5_cfb64            "RC5-CFB"
#define LN_rc5_cfb64            "rc5-cfb"
#define NID_rc5_cfb64           122

#define SN_rc5_ofb64            "RC5-OFB"
#define LN_rc5_ofb64            "rc5-ofb"
#define NID_rc5_ofb64           123

#define SN_ms_ext_req           "msExtReq"
#define LN_ms_ext_req           "Microsoft Extension Request"
#define NID_ms_ext_req          171
#define OBJ_ms_ext_req          1L,3L,6L,1L,4L,1L,311L,2L,1L,14L

#define SN_ms_code_ind          "msCodeInd"
#define LN_ms_code_ind          "Microsoft Individual Code Signing"
#define NID_ms_code_ind         134
#define OBJ_ms_code_ind         1L,3L,6L,1L,4L,1L,311L,2L,1L,21L

#define SN_ms_code_com          "msCodeCom"
#define LN_ms_code_com          "Microsoft Commercial Code Signing"
#define NID_ms_code_com         135
#define OBJ_ms_code_com         1L,3L,6L,1L,4L,1L,311L,2L,1L,22L

#define SN_ms_ctl_sign          "msCTLSign"
#define LN_ms_ctl_sign          "Microsoft Trust List Signing"
#define NID_ms_ctl_sign         136
#define OBJ_ms_ctl_sign         1L,3L,6L,1L,4L,1L,311L,10L,3L,1L

#define SN_ms_sgc               "msSGC"
#define LN_ms_sgc               "Microsoft Server Gated Crypto"
#define NID_ms_sgc              137
#define OBJ_ms_sgc              1L,3L,6L,1L,4L,1L,311L,10L,3L,3L

#define SN_ms_efs               "msEFS"
#define LN_ms_efs               "Microsoft Encrypted File System"
#define NID_ms_efs              138
#define OBJ_ms_efs              1L,3L,6L,1L,4L,1L,311L,10L,3L,4L

#define SN_ms_smartcard_login           "msSmartcardLogin"
#define LN_ms_smartcard_login           "Microsoft Smartcardlogin"
#define NID_ms_smartcard_login          648
#define OBJ_ms_smartcard_login          1L,3L,6L,1L,4L,1L,311L,20L,2L,2L

#define SN_ms_upn               "msUPN"
#define LN_ms_upn               "Microsoft Universal Principal Name"
#define NID_ms_upn              649
#define OBJ_ms_upn              1L,3L,6L,1L,4L,1L,311L,20L,2L,3L

#define SN_idea_cbc             "IDEA-CBC"
#define LN_idea_cbc             "idea-cbc"
#define NID_idea_cbc            34
#define OBJ_idea_cbc            1L,3L,6L,1L,4L,1L,188L,7L,1L,1L,2L

#define SN_idea_ecb             "IDEA-ECB"
#define LN_idea_ecb             "idea-ecb"
#define NID_idea_ecb            36

#define SN_idea_cfb64           "IDEA-CFB"
#define LN_idea_cfb64           "idea-cfb"
#define NID_idea_cfb64          35

#define SN_idea_ofb64           "IDEA-OFB"
#define LN_idea_ofb64           "idea-ofb"
#define NID_idea_ofb64          46

#define SN_bf_cbc               "BF-CBC"
#define LN_bf_cbc               "bf-cbc"
#define NID_bf_cbc              91
#define OBJ_bf_cbc              1L,3L,6L,1L,4L,1L,3029L,1L,2L

#define SN_bf_ecb               "BF-ECB"
#define LN_bf_ecb               "bf-ecb"
#define NID_bf_ecb              92

#define SN_bf_cfb64             "BF-CFB"
#define LN_bf_cfb64             "bf-cfb"
#define NID_bf_cfb64            93

#define SN_bf_ofb64             "BF-OFB"
#define LN_bf_ofb64             "bf-ofb"
#define NID_bf_ofb64            94

#define SN_id_pkix              "PKIX"
#define NID_id_pkix             127
#define OBJ_id_pkix             1L,3L,6L,1L,5L,5L,7L

#define SN_id_pkix_mod          "id-pkix-mod"
#define NID_id_pkix_mod         258
#define OBJ_id_pkix_mod         OBJ_id_pkix,0L

#define SN_id_pe                "id-pe"
#define NID_id_pe               175
#define OBJ_id_pe               OBJ_id_pkix,1L

#define SN_id_qt                "id-qt"
#define NID_id_qt               259
#define OBJ_id_qt               OBJ_id_pkix,2L

#define SN_id_kp                "id-kp"
#define NID_id_kp               128
#define OBJ_id_kp               OBJ_id_pkix,3L

#define SN_id_it                "id-it"
#define NID_id_it               260
#define OBJ_id_it               OBJ_id_pkix,4L

#define SN_id_pkip              "id-pkip"
#define NID_id_pkip             261
#define OBJ_id_pkip             OBJ_id_pkix,5L

#define SN_id_alg               "id-alg"
#define NID_id_alg              262
#define OBJ_id_alg              OBJ_id_pkix,6L

#define SN_id_cmc               "id-cmc"
#define NID_id_cmc              263
#define OBJ_id_cmc              OBJ_id_pkix,7L

#define SN_id_on                "id-on"
#define NID_id_on               264
#define OBJ_id_on               OBJ_id_pkix,8L

#define SN_id_pda               "id-pda"
#define NID_id_pda              265
#define OBJ_id_pda              OBJ_id_pkix,9L

#define SN_id_aca               "id-aca"
#define NID_id_aca              266
#define OBJ_id_aca              OBJ_id_pkix,10L

#define SN_id_qcs               "id-qcs"
#define NID_id_qcs              267
#define OBJ_id_qcs              OBJ_id_pkix,11L

#define SN_id_cct               "id-cct"
#define NID_id_cct              268
#define OBJ_id_cct              OBJ_id_pkix,12L

#define SN_id_ppl               "id-ppl"
#define NID_id_ppl              662
#define OBJ_id_ppl              OBJ_id_pkix,21L

#define SN_id_ad                "id-ad"
#define NID_id_ad               176
#define OBJ_id_ad               OBJ_id_pkix,48L

#define SN_id_pkix1_explicit_88         "id-pkix1-explicit-88"
#define NID_id_pkix1_explicit_88                269
#define OBJ_id_pkix1_explicit_88                OBJ_id_pkix_mod,1L

#define SN_id_pkix1_implicit_88         "id-pkix1-implicit-88"
#define NID_id_pkix1_implicit_88                270
#define OBJ_id_pkix1_implicit_88                OBJ_id_pkix_mod,2L

#define SN_id_pkix1_explicit_93         "id-pkix1-explicit-93"
#define NID_id_pkix1_explicit_93                271
#define OBJ_id_pkix1_explicit_93                OBJ_id_pkix_mod,3L

#define SN_id_pkix1_implicit_93         "id-pkix1-implicit-93"
#define NID_id_pkix1_implicit_93                272
#define OBJ_id_pkix1_implicit_93                OBJ_id_pkix_mod,4L

#define SN_id_mod_crmf          "id-mod-crmf"
#define NID_id_mod_crmf         273
#define OBJ_id_mod_crmf         OBJ_id_pkix_mod,5L

#define SN_id_mod_cmc           "id-mod-cmc"
#define NID_id_mod_cmc          274
#define OBJ_id_mod_cmc          OBJ_id_pkix_mod,6L

#define SN_id_mod_kea_profile_88                "id-mod-kea-profile-88"
#define NID_id_mod_kea_profile_88               275
#define OBJ_id_mod_kea_profile_88               OBJ_id_pkix_mod,7L

#define SN_id_mod_kea_profile_93                "id-mod-kea-profile-93"
#define NID_id_mod_kea_profile_93               276
#define OBJ_id_mod_kea_profile_93               OBJ_id_pkix_mod,8L

#define SN_id_mod_cmp           "id-mod-cmp"
#define NID_id_mod_cmp          277
#define OBJ_id_mod_cmp          OBJ_id_pkix_mod,9L

#define SN_id_mod_qualified_cert_88             "id-mod-qualified-cert-88"
#define NID_id_mod_qualified_cert_88            278
#define OBJ_id_mod_qualified_cert_88            OBJ_id_pkix_mod,10L

#define SN_id_mod_qualified_cert_93             "id-mod-qualified-cert-93"
#define NID_id_mod_qualified_cert_93            279
#define OBJ_id_mod_qualified_cert_93            OBJ_id_pkix_mod,11L

#define SN_id_mod_attribute_cert                "id-mod-attribute-cert"
#define NID_id_mod_attribute_cert               280
#define OBJ_id_mod_attribute_cert               OBJ_id_pkix_mod,12L

#define SN_id_mod_timestamp_protocol            "id-mod-timestamp-protocol"
#define NID_id_mod_timestamp_protocol           281
#define OBJ_id_mod_timestamp_protocol           OBJ_id_pkix_mod,13L

#define SN_id_mod_ocsp          "id-mod-ocsp"
#define NID_id_mod_ocsp         282
#define OBJ_id_mod_ocsp         OBJ_id_pkix_mod,14L

#define SN_id_mod_dvcs          "id-mod-dvcs"
#define NID_id_mod_dvcs         283
#define OBJ_id_mod_dvcs         OBJ_id_pkix_mod,15L

#define SN_id_mod_cmp2000               "id-mod-cmp2000"
#define NID_id_mod_cmp2000              284
#define OBJ_id_mod_cmp2000              OBJ_id_pkix_mod,16L

#define SN_info_access          "authorityInfoAccess"
#define LN_info_access          "Authority Information Access"
#define NID_info_access         177
#define OBJ_info_access         OBJ_id_pe,1L

#define SN_biometricInfo                "biometricInfo"
#define LN_biometricInfo                "Biometric Info"
#define NID_biometricInfo               285
#define OBJ_biometricInfo               OBJ_id_pe,2L

#define SN_qcStatements         "qcStatements"
#define NID_qcStatements                286
#define OBJ_qcStatements                OBJ_id_pe,3L

#define SN_ac_auditEntity               "ac-auditEntity"
#define NID_ac_auditEntity              287
#define OBJ_ac_auditEntity              OBJ_id_pe,4L

#define SN_ac_targeting         "ac-targeting"
#define NID_ac_targeting                288
#define OBJ_ac_targeting                OBJ_id_pe,5L

#define SN_aaControls           "aaControls"
#define NID_aaControls          289
#define OBJ_aaControls          OBJ_id_pe,6L

#define SN_sbgp_ipAddrBlock             "sbgp-ipAddrBlock"
#define NID_sbgp_ipAddrBlock            290
#define OBJ_sbgp_ipAddrBlock            OBJ_id_pe,7L

#define SN_sbgp_autonomousSysNum                "sbgp-autonomousSysNum"
#define NID_sbgp_autonomousSysNum               291
#define OBJ_sbgp_autonomousSysNum               OBJ_id_pe,8L

#define SN_sbgp_routerIdentifier                "sbgp-routerIdentifier"
#define NID_sbgp_routerIdentifier               292
#define OBJ_sbgp_routerIdentifier               OBJ_id_pe,9L

#define SN_ac_proxying          "ac-proxying"
#define NID_ac_proxying         397
#define OBJ_ac_proxying         OBJ_id_pe,10L

#define SN_sinfo_access         "subjectInfoAccess"
#define LN_sinfo_access         "Subject Information Access"
#define NID_sinfo_access                398
#define OBJ_sinfo_access                OBJ_id_pe,11L

#define SN_proxyCertInfo                "proxyCertInfo"
#define LN_proxyCertInfo                "Proxy Certificate Information"
#define NID_proxyCertInfo               663
#define OBJ_proxyCertInfo               OBJ_id_pe,14L

#define SN_id_qt_cps            "id-qt-cps"
#define LN_id_qt_cps            "Policy Qualifier CPS"
#define NID_id_qt_cps           164
#define OBJ_id_qt_cps           OBJ_id_qt,1L

#define SN_id_qt_unotice                "id-qt-unotice"
#define LN_id_qt_unotice                "Policy Qualifier User Notice"
#define NID_id_qt_unotice               165
#define OBJ_id_qt_unotice               OBJ_id_qt,2L

#define SN_textNotice           "textNotice"
#define NID_textNotice          293
#define OBJ_textNotice          OBJ_id_qt,3L

#define SN_server_auth          "serverAuth"
#define LN_server_auth          "TLS Web Server Authentication"
#define NID_server_auth         129
#define OBJ_server_auth         OBJ_id_kp,1L

#define SN_client_auth          "clientAuth"
#define LN_client_auth          "TLS Web Client Authentication"
#define NID_client_auth         130
#define OBJ_client_auth         OBJ_id_kp,2L

#define SN_code_sign            "codeSigning"
#define LN_code_sign            "Code Signing"
#define NID_code_sign           131
#define OBJ_code_sign           OBJ_id_kp,3L

#define SN_email_protect                "emailProtection"
#define LN_email_protect                "E-mail Protection"
#define NID_email_protect               132
#define OBJ_email_protect               OBJ_id_kp,4L

#define SN_ipsecEndSystem               "ipsecEndSystem"
#define LN_ipsecEndSystem               "IPSec End System"
#define NID_ipsecEndSystem              294
#define OBJ_ipsecEndSystem              OBJ_id_kp,5L

#define SN_ipsecTunnel          "ipsecTunnel"
#define LN_ipsecTunnel          "IPSec Tunnel"
#define NID_ipsecTunnel         295
#define OBJ_ipsecTunnel         OBJ_id_kp,6L

#define SN_ipsecUser            "ipsecUser"
#define LN_ipsecUser            "IPSec User"
#define NID_ipsecUser           296
#define OBJ_ipsecUser           OBJ_id_kp,7L

#define SN_time_stamp           "timeStamping"
#define LN_time_stamp           "Time Stamping"
#define NID_time_stamp          133
#define OBJ_time_stamp          OBJ_id_kp,8L

#define SN_OCSP_sign            "OCSPSigning"
#define LN_OCSP_sign            "OCSP Signing"
#define NID_OCSP_sign           180
#define OBJ_OCSP_sign           OBJ_id_kp,9L

#define SN_dvcs         "DVCS"
#define LN_dvcs         "dvcs"
#define NID_dvcs                297
#define OBJ_dvcs                OBJ_id_kp,10L

#define SN_id_it_caProtEncCert          "id-it-caProtEncCert"
#define NID_id_it_caProtEncCert         298
#define OBJ_id_it_caProtEncCert         OBJ_id_it,1L

#define SN_id_it_signKeyPairTypes               "id-it-signKeyPairTypes"
#define NID_id_it_signKeyPairTypes              299
#define OBJ_id_it_signKeyPairTypes              OBJ_id_it,2L

#define SN_id_it_encKeyPairTypes                "id-it-encKeyPairTypes"
#define NID_id_it_encKeyPairTypes               300
#define OBJ_id_it_encKeyPairTypes               OBJ_id_it,3L

#define SN_id_it_preferredSymmAlg               "id-it-preferredSymmAlg"
#define NID_id_it_preferredSymmAlg              301
#define OBJ_id_it_preferredSymmAlg              OBJ_id_it,4L

#define SN_id_it_caKeyUpdateInfo                "id-it-caKeyUpdateInfo"
#define NID_id_it_caKeyUpdateInfo               302
#define OBJ_id_it_caKeyUpdateInfo               OBJ_id_it,5L

#define SN_id_it_currentCRL             "id-it-currentCRL"
#define NID_id_it_currentCRL            303
#define OBJ_id_it_currentCRL            OBJ_id_it,6L

#define SN_id_it_unsupportedOIDs                "id-it-unsupportedOIDs"
#define NID_id_it_unsupportedOIDs               304
#define OBJ_id_it_unsupportedOIDs               OBJ_id_it,7L

#define SN_id_it_subscriptionRequest            "id-it-subscriptionRequest"
#define NID_id_it_subscriptionRequest           305
#define OBJ_id_it_subscriptionRequest           OBJ_id_it,8L

#define SN_id_it_subscriptionResponse           "id-it-subscriptionResponse"
#define NID_id_it_subscriptionResponse          306
#define OBJ_id_it_subscriptionResponse          OBJ_id_it,9L

#define SN_id_it_keyPairParamReq                "id-it-keyPairParamReq"
#define NID_id_it_keyPairParamReq               307
#define OBJ_id_it_keyPairParamReq               OBJ_id_it,10L

#define SN_id_it_keyPairParamRep                "id-it-keyPairParamRep"
#define NID_id_it_keyPairParamRep               308
#define OBJ_id_it_keyPairParamRep               OBJ_id_it,11L

#define SN_id_it_revPassphrase          "id-it-revPassphrase"
#define NID_id_it_revPassphrase         309
#define OBJ_id_it_revPassphrase         OBJ_id_it,12L

#define SN_id_it_implicitConfirm                "id-it-implicitConfirm"
#define NID_id_it_implicitConfirm               310
#define OBJ_id_it_implicitConfirm               OBJ_id_it,13L

#define SN_id_it_confirmWaitTime                "id-it-confirmWaitTime"
#define NID_id_it_confirmWaitTime               311
#define OBJ_id_it_confirmWaitTime               OBJ_id_it,14L

#define SN_id_it_origPKIMessage         "id-it-origPKIMessage"
#define NID_id_it_origPKIMessage                312
#define OBJ_id_it_origPKIMessage                OBJ_id_it,15L

#define SN_id_it_suppLangTags           "id-it-suppLangTags"
#define NID_id_it_suppLangTags          784
#define OBJ_id_it_suppLangTags          OBJ_id_it,16L

#define SN_id_regCtrl           "id-regCtrl"
#define NID_id_regCtrl          313
#define OBJ_id_regCtrl          OBJ_id_pkip,1L

#define SN_id_regInfo           "id-regInfo"
#define NID_id_regInfo          314
#define OBJ_id_regInfo          OBJ_id_pkip,2L

#define SN_id_regCtrl_regToken          "id-regCtrl-regToken"
#define NID_id_regCtrl_regToken         315
#define OBJ_id_regCtrl_regToken         OBJ_id_regCtrl,1L

#define SN_id_regCtrl_authenticator             "id-regCtrl-authenticator"
#define NID_id_regCtrl_authenticator            316
#define OBJ_id_regCtrl_authenticator            OBJ_id_regCtrl,2L

#define SN_id_regCtrl_pkiPublicationInfo                "id-regCtrl-pkiPublicationInfo"
#define NID_id_regCtrl_pkiPublicationInfo               317
#define OBJ_id_regCtrl_pkiPublicationInfo               OBJ_id_regCtrl,3L

#define SN_id_regCtrl_pkiArchiveOptions         "id-regCtrl-pkiArchiveOptions"
#define NID_id_regCtrl_pkiArchiveOptions                318
#define OBJ_id_regCtrl_pkiArchiveOptions                OBJ_id_regCtrl,4L

#define SN_id_regCtrl_oldCertID         "id-regCtrl-oldCertID"
#define NID_id_regCtrl_oldCertID                319
#define OBJ_id_regCtrl_oldCertID                OBJ_id_regCtrl,5L

#define SN_id_regCtrl_protocolEncrKey           "id-regCtrl-protocolEncrKey"
#define NID_id_regCtrl_protocolEncrKey          320
#define OBJ_id_regCtrl_protocolEncrKey          OBJ_id_regCtrl,6L

#define SN_id_regInfo_utf8Pairs         "id-regInfo-utf8Pairs"
#define NID_id_regInfo_utf8Pairs                321
#define OBJ_id_regInfo_utf8Pairs                OBJ_id_regInfo,1L

#define SN_id_regInfo_certReq           "id-regInfo-certReq"
#define NID_id_regInfo_certReq          322
#define OBJ_id_regInfo_certReq          OBJ_id_regInfo,2L

#define SN_id_alg_des40         "id-alg-des40"
#define NID_id_alg_des40                323
#define OBJ_id_alg_des40                OBJ_id_alg,1L

#define SN_id_alg_noSignature           "id-alg-noSignature"
#define NID_id_alg_noSignature          324
#define OBJ_id_alg_noSignature          OBJ_id_alg,2L

#define SN_id_alg_dh_sig_hmac_sha1              "id-alg-dh-sig-hmac-sha1"
#define NID_id_alg_dh_sig_hmac_sha1             325
#define OBJ_id_alg_dh_sig_hmac_sha1             OBJ_id_alg,3L

#define SN_id_alg_dh_pop                "id-alg-dh-pop"
#define NID_id_alg_dh_pop               326
#define OBJ_id_alg_dh_pop               OBJ_id_alg,4L

#define SN_id_cmc_statusInfo            "id-cmc-statusInfo"
#define NID_id_cmc_statusInfo           327
#define OBJ_id_cmc_statusInfo           OBJ_id_cmc,1L

#define SN_id_cmc_identification                "id-cmc-identification"
#define NID_id_cmc_identification               328
#define OBJ_id_cmc_identification               OBJ_id_cmc,2L

#define SN_id_cmc_identityProof         "id-cmc-identityProof"
#define NID_id_cmc_identityProof                329
#define OBJ_id_cmc_identityProof                OBJ_id_cmc,3L

#define SN_id_cmc_dataReturn            "id-cmc-dataReturn"
#define NID_id_cmc_dataReturn           330
#define OBJ_id_cmc_dataReturn           OBJ_id_cmc,4L

#define SN_id_cmc_transactionId         "id-cmc-transactionId"
#define NID_id_cmc_transactionId                331
#define OBJ_id_cmc_transactionId                OBJ_id_cmc,5L

#define SN_id_cmc_senderNonce           "id-cmc-senderNonce"
#define NID_id_cmc_senderNonce          332
#define OBJ_id_cmc_senderNonce          OBJ_id_cmc,6L

#define SN_id_cmc_recipientNonce                "id-cmc-recipientNonce"
#define NID_id_cmc_recipientNonce               333
#define OBJ_id_cmc_recipientNonce               OBJ_id_cmc,7L

#define SN_id_cmc_addExtensions         "id-cmc-addExtensions"
#define NID_id_cmc_addExtensions                334
#define OBJ_id_cmc_addExtensions                OBJ_id_cmc,8L

#define SN_id_cmc_encryptedPOP          "id-cmc-encryptedPOP"
#define NID_id_cmc_encryptedPOP         335
#define OBJ_id_cmc_encryptedPOP         OBJ_id_cmc,9L

#define SN_id_cmc_decryptedPOP          "id-cmc-decryptedPOP"
#define NID_id_cmc_decryptedPOP         336
#define OBJ_id_cmc_decryptedPOP         OBJ_id_cmc,10L

#define SN_id_cmc_lraPOPWitness         "id-cmc-lraPOPWitness"
#define NID_id_cmc_lraPOPWitness                337
#define OBJ_id_cmc_lraPOPWitness                OBJ_id_cmc,11L

#define SN_id_cmc_getCert               "id-cmc-getCert"
#define NID_id_cmc_getCert              338
#define OBJ_id_cmc_getCert              OBJ_id_cmc,15L

#define SN_id_cmc_getCRL                "id-cmc-getCRL"
#define NID_id_cmc_getCRL               339
#define OBJ_id_cmc_getCRL               OBJ_id_cmc,16L

#define SN_id_cmc_revokeRequest         "id-cmc-revokeRequest"
#define NID_id_cmc_revokeRequest                340
#define OBJ_id_cmc_revokeRequest                OBJ_id_cmc,17L

#define SN_id_cmc_regInfo               "id-cmc-regInfo"
#define NID_id_cmc_regInfo              341
#define OBJ_id_cmc_regInfo              OBJ_id_cmc,18L

#define SN_id_cmc_responseInfo          "id-cmc-responseInfo"
#define NID_id_cmc_responseInfo         342
#define OBJ_id_cmc_responseInfo         OBJ_id_cmc,19L

#define SN_id_cmc_queryPending          "id-cmc-queryPending"
#define NID_id_cmc_queryPending         343
#define OBJ_id_cmc_queryPending         OBJ_id_cmc,21L

#define SN_id_cmc_popLinkRandom         "id-cmc-popLinkRandom"
#define NID_id_cmc_popLinkRandom                344
#define OBJ_id_cmc_popLinkRandom                OBJ_id_cmc,22L

#define SN_id_cmc_popLinkWitness                "id-cmc-popLinkWitness"
#define NID_id_cmc_popLinkWitness               345
#define OBJ_id_cmc_popLinkWitness               OBJ_id_cmc,23L

#define SN_id_cmc_confirmCertAcceptance         "id-cmc-confirmCertAcceptance"
#define NID_id_cmc_confirmCertAcceptance                346
#define OBJ_id_cmc_confirmCertAcceptance                OBJ_id_cmc,24L

#define SN_id_on_personalData           "id-on-personalData"
#define NID_id_on_personalData          347
#define OBJ_id_on_personalData          OBJ_id_on,1L

#define SN_id_on_permanentIdentifier            "id-on-permanentIdentifier"
#define LN_id_on_permanentIdentifier            "Permanent Identifier"
#define NID_id_on_permanentIdentifier           858
#define OBJ_id_on_permanentIdentifier           OBJ_id_on,3L

#define SN_id_pda_dateOfBirth           "id-pda-dateOfBirth"
#define NID_id_pda_dateOfBirth          348
#define OBJ_id_pda_dateOfBirth          OBJ_id_pda,1L

#define SN_id_pda_placeOfBirth          "id-pda-placeOfBirth"
#define NID_id_pda_placeOfBirth         349
#define OBJ_id_pda_placeOfBirth         OBJ_id_pda,2L

#define SN_id_pda_gender                "id-pda-gender"
#define NID_id_pda_gender               351
#define OBJ_id_pda_gender               OBJ_id_pda,3L

#define SN_id_pda_countryOfCitizenship          "id-pda-countryOfCitizenship"
#define NID_id_pda_countryOfCitizenship         352
#define OBJ_id_pda_countryOfCitizenship         OBJ_id_pda,4L

#define SN_id_pda_countryOfResidence            "id-pda-countryOfResidence"
#define NID_id_pda_countryOfResidence           353
#define OBJ_id_pda_countryOfResidence           OBJ_id_pda,5L

#define SN_id_aca_authenticationInfo            "id-aca-authenticationInfo"
#define NID_id_aca_authenticationInfo           354
#define OBJ_id_aca_authenticationInfo           OBJ_id_aca,1L

#define SN_id_aca_accessIdentity                "id-aca-accessIdentity"
#define NID_id_aca_accessIdentity               355
#define OBJ_id_aca_accessIdentity               OBJ_id_aca,2L

#define SN_id_aca_chargingIdentity              "id-aca-chargingIdentity"
#define NID_id_aca_chargingIdentity             356
#define OBJ_id_aca_chargingIdentity             OBJ_id_aca,3L

#define SN_id_aca_group         "id-aca-group"
#define NID_id_aca_group                357
#define OBJ_id_aca_group                OBJ_id_aca,4L

#define SN_id_aca_role          "id-aca-role"
#define NID_id_aca_role         358
#define OBJ_id_aca_role         OBJ_id_aca,5L

#define SN_id_aca_encAttrs              "id-aca-encAttrs"
#define NID_id_aca_encAttrs             399
#define OBJ_id_aca_encAttrs             OBJ_id_aca,6L

#define SN_id_qcs_pkixQCSyntax_v1               "id-qcs-pkixQCSyntax-v1"
#define NID_id_qcs_pkixQCSyntax_v1              359
#define OBJ_id_qcs_pkixQCSyntax_v1              OBJ_id_qcs,1L

#define SN_id_cct_crs           "id-cct-crs"
#define NID_id_cct_crs          360
#define OBJ_id_cct_crs          OBJ_id_cct,1L

#define SN_id_cct_PKIData               "id-cct-PKIData"
#define NID_id_cct_PKIData              361
#define OBJ_id_cct_PKIData              OBJ_id_cct,2L

#define SN_id_cct_PKIResponse           "id-cct-PKIResponse"
#define NID_id_cct_PKIResponse          362
#define OBJ_id_cct_PKIResponse          OBJ_id_cct,3L

#define SN_id_ppl_anyLanguage           "id-ppl-anyLanguage"
#define LN_id_ppl_anyLanguage           "Any language"
#define NID_id_ppl_anyLanguage          664
#define OBJ_id_ppl_anyLanguage          OBJ_id_ppl,0L

#define SN_id_ppl_inheritAll            "id-ppl-inheritAll"
#define LN_id_ppl_inheritAll            "Inherit all"
#define NID_id_ppl_inheritAll           665
#define OBJ_id_ppl_inheritAll           OBJ_id_ppl,1L

#define SN_Independent          "id-ppl-independent"
#define LN_Independent          "Independent"
#define NID_Independent         667
#define OBJ_Independent         OBJ_id_ppl,2L

#define SN_ad_OCSP              "OCSP"
#define LN_ad_OCSP              "OCSP"
#define NID_ad_OCSP             178
#define OBJ_ad_OCSP             OBJ_id_ad,1L

#define SN_ad_ca_issuers                "caIssuers"
#define LN_ad_ca_issuers                "CA Issuers"
#define NID_ad_ca_issuers               179
#define OBJ_ad_ca_issuers               OBJ_id_ad,2L

#define SN_ad_timeStamping              "ad_timestamping"
#define LN_ad_timeStamping              "AD Time Stamping"
#define NID_ad_timeStamping             363
#define OBJ_ad_timeStamping             OBJ_id_ad,3L

#define SN_ad_dvcs              "AD_DVCS"
#define LN_ad_dvcs              "ad dvcs"
#define NID_ad_dvcs             364
#define OBJ_ad_dvcs             OBJ_id_ad,4L

#define SN_caRepository         "caRepository"
#define LN_caRepository         "CA Repository"
#define NID_caRepository                785
#define OBJ_caRepository                OBJ_id_ad,5L

#define OBJ_id_pkix_OCSP                OBJ_ad_OCSP

#define SN_id_pkix_OCSP_basic           "basicOCSPResponse"
#define LN_id_pkix_OCSP_basic           "Basic OCSP Response"
#define NID_id_pkix_OCSP_basic          365
#define OBJ_id_pkix_OCSP_basic          OBJ_id_pkix_OCSP,1L

#define SN_id_pkix_OCSP_Nonce           "Nonce"
#define LN_id_pkix_OCSP_Nonce           "OCSP Nonce"
#define NID_id_pkix_OCSP_Nonce          366
#define OBJ_id_pkix_OCSP_Nonce          OBJ_id_pkix_OCSP,2L

#define SN_id_pkix_OCSP_CrlID           "CrlID"
#define LN_id_pkix_OCSP_CrlID           "OCSP CRL ID"
#define NID_id_pkix_OCSP_CrlID          367
#define OBJ_id_pkix_OCSP_CrlID          OBJ_id_pkix_OCSP,3L

#define SN_id_pkix_OCSP_acceptableResponses             "acceptableResponses"
#define LN_id_pkix_OCSP_acceptableResponses             "Acceptable OCSP Responses"
#define NID_id_pkix_OCSP_acceptableResponses            368
#define OBJ_id_pkix_OCSP_acceptableResponses            OBJ_id_pkix_OCSP,4L

#define SN_id_pkix_OCSP_noCheck         "noCheck"
#define LN_id_pkix_OCSP_noCheck         "OCSP No Check"
#define NID_id_pkix_OCSP_noCheck                369
#define OBJ_id_pkix_OCSP_noCheck                OBJ_id_pkix_OCSP,5L

#define SN_id_pkix_OCSP_archiveCutoff           "archiveCutoff"
#define LN_id_pkix_OCSP_archiveCutoff           "OCSP Archive Cutoff"
#define NID_id_pkix_OCSP_archiveCutoff          370
#define OBJ_id_pkix_OCSP_archiveCutoff          OBJ_id_pkix_OCSP,6L

#define SN_id_pkix_OCSP_serviceLocator          "serviceLocator"
#define LN_id_pkix_OCSP_serviceLocator          "OCSP Service Locator"
#define NID_id_pkix_OCSP_serviceLocator         371
#define OBJ_id_pkix_OCSP_serviceLocator         OBJ_id_pkix_OCSP,7L

#define SN_id_pkix_OCSP_extendedStatus          "extendedStatus"
#define LN_id_pkix_OCSP_extendedStatus          "Extended OCSP Status"
#define NID_id_pkix_OCSP_extendedStatus         372
#define OBJ_id_pkix_OCSP_extendedStatus         OBJ_id_pkix_OCSP,8L

#define SN_id_pkix_OCSP_valid           "valid"
#define NID_id_pkix_OCSP_valid          373
#define OBJ_id_pkix_OCSP_valid          OBJ_id_pkix_OCSP,9L

#define SN_id_pkix_OCSP_path            "path"
#define NID_id_pkix_OCSP_path           374
#define OBJ_id_pkix_OCSP_path           OBJ_id_pkix_OCSP,10L

#define SN_id_pkix_OCSP_trustRoot               "trustRoot"
#define LN_id_pkix_OCSP_trustRoot               "Trust Root"
#define NID_id_pkix_OCSP_trustRoot              375
#define OBJ_id_pkix_OCSP_trustRoot              OBJ_id_pkix_OCSP,11L

#define SN_algorithm            "algorithm"
#define LN_algorithm            "algorithm"
#define NID_algorithm           376
#define OBJ_algorithm           1L,3L,14L,3L,2L

#define SN_md5WithRSA           "RSA-NP-MD5"
#define LN_md5WithRSA           "md5WithRSA"
#define NID_md5WithRSA          104
#define OBJ_md5WithRSA          OBJ_algorithm,3L

#define SN_des_ecb              "DES-ECB"
#define LN_des_ecb              "des-ecb"
#define NID_des_ecb             29
#define OBJ_des_ecb             OBJ_algorithm,6L

#define SN_des_cbc              "DES-CBC"
#define LN_des_cbc              "des-cbc"
#define NID_des_cbc             31
#define OBJ_des_cbc             OBJ_algorithm,7L

#define SN_des_ofb64            "DES-OFB"
#define LN_des_ofb64            "des-ofb"
#define NID_des_ofb64           45
#define OBJ_des_ofb64           OBJ_algorithm,8L

#define SN_des_cfb64            "DES-CFB"
#define LN_des_cfb64            "des-cfb"
#define NID_des_cfb64           30
#define OBJ_des_cfb64           OBJ_algorithm,9L

#define SN_rsaSignature         "rsaSignature"
#define NID_rsaSignature                377
#define OBJ_rsaSignature                OBJ_algorithm,11L

#define SN_dsa_2                "DSA-old"
#define LN_dsa_2                "dsaEncryption-old"
#define NID_dsa_2               67
#define OBJ_dsa_2               OBJ_algorithm,12L

#define SN_dsaWithSHA           "DSA-SHA"
#define LN_dsaWithSHA           "dsaWithSHA"
#define NID_dsaWithSHA          66
#define OBJ_dsaWithSHA          OBJ_algorithm,13L

#define SN_shaWithRSAEncryption         "RSA-SHA"
#define LN_shaWithRSAEncryption         "shaWithRSAEncryption"
#define NID_shaWithRSAEncryption                42
#define OBJ_shaWithRSAEncryption                OBJ_algorithm,15L

#define SN_des_ede_ecb          "DES-EDE"
#define LN_des_ede_ecb          "des-ede"
#define NID_des_ede_ecb         32
#define OBJ_des_ede_ecb         OBJ_algorithm,17L

#define SN_des_ede3_ecb         "DES-EDE3"
#define LN_des_ede3_ecb         "des-ede3"
#define NID_des_ede3_ecb                33

#define SN_des_ede_cbc          "DES-EDE-CBC"
#define LN_des_ede_cbc          "des-ede-cbc"
#define NID_des_ede_cbc         43

#define SN_des_ede_cfb64                "DES-EDE-CFB"
#define LN_des_ede_cfb64                "des-ede-cfb"
#define NID_des_ede_cfb64               60

#define SN_des_ede3_cfb64               "DES-EDE3-CFB"
#define LN_des_ede3_cfb64               "des-ede3-cfb"
#define NID_des_ede3_cfb64              61

#define SN_des_ede_ofb64                "DES-EDE-OFB"
#define LN_des_ede_ofb64                "des-ede-ofb"
#define NID_des_ede_ofb64               62

#define SN_des_ede3_ofb64               "DES-EDE3-OFB"
#define LN_des_ede3_ofb64               "des-ede3-ofb"
#define NID_des_ede3_ofb64              63

#define SN_desx_cbc             "DESX-CBC"
#define LN_desx_cbc             "desx-cbc"
#define NID_desx_cbc            80

#define SN_sha          "SHA"
#define LN_sha          "sha"
#define NID_sha         41
#define OBJ_sha         OBJ_algorithm,18L

#define SN_sha1         "SHA1"
#define LN_sha1         "sha1"
#define NID_sha1                64
#define OBJ_sha1                OBJ_algorithm,26L

#define SN_dsaWithSHA1_2                "DSA-SHA1-old"
#define LN_dsaWithSHA1_2                "dsaWithSHA1-old"
#define NID_dsaWithSHA1_2               70
#define OBJ_dsaWithSHA1_2               OBJ_algorithm,27L

#define SN_sha1WithRSA          "RSA-SHA1-2"
#define LN_sha1WithRSA          "sha1WithRSA"
#define NID_sha1WithRSA         115
#define OBJ_sha1WithRSA         OBJ_algorithm,29L

#define SN_ripemd160            "RIPEMD160"
#define LN_ripemd160            "ripemd160"
#define NID_ripemd160           117
#define OBJ_ripemd160           1L,3L,36L,3L,2L,1L

#define SN_ripemd160WithRSA             "RSA-RIPEMD160"
#define LN_ripemd160WithRSA             "ripemd160WithRSA"
#define NID_ripemd160WithRSA            119
#define OBJ_ripemd160WithRSA            1L,3L,36L,3L,3L,1L,2L

#define SN_sxnet                "SXNetID"
#define LN_sxnet                "Strong Extranet ID"
#define NID_sxnet               143
#define OBJ_sxnet               1L,3L,101L,1L,4L,1L

#define SN_X500         "X500"
#define LN_X500         "directory services (X.500)"
#define NID_X500                11
#define OBJ_X500                2L,5L

#define SN_X509         "X509"
#define NID_X509                12
#define OBJ_X509                OBJ_X500,4L

#define SN_commonName           "CN"
#define LN_commonName           "commonName"
#define NID_commonName          13
#define OBJ_commonName          OBJ_X509,3L

#define SN_surname              "SN"
#define LN_surname              "surname"
#define NID_surname             100
#define OBJ_surname             OBJ_X509,4L

#define LN_serialNumber         "serialNumber"
#define NID_serialNumber                105
#define OBJ_serialNumber                OBJ_X509,5L

#define SN_countryName          "C"
#define LN_countryName          "countryName"
#define NID_countryName         14
#define OBJ_countryName         OBJ_X509,6L

#define SN_localityName         "L"
#define LN_localityName         "localityName"
#define NID_localityName                15
#define OBJ_localityName                OBJ_X509,7L

#define SN_stateOrProvinceName          "ST"
#define LN_stateOrProvinceName          "stateOrProvinceName"
#define NID_stateOrProvinceName         16
#define OBJ_stateOrProvinceName         OBJ_X509,8L

#define SN_streetAddress                "street"
#define LN_streetAddress                "streetAddress"
#define NID_streetAddress               660
#define OBJ_streetAddress               OBJ_X509,9L

#define SN_organizationName             "O"
#define LN_organizationName             "organizationName"
#define NID_organizationName            17
#define OBJ_organizationName            OBJ_X509,10L

#define SN_organizationalUnitName               "OU"
#define LN_organizationalUnitName               "organizationalUnitName"
#define NID_organizationalUnitName              18
#define OBJ_organizationalUnitName              OBJ_X509,11L

#define SN_title                "title"
#define LN_title                "title"
#define NID_title               106
#define OBJ_title               OBJ_X509,12L

#define LN_description          "description"
#define NID_description         107
#define OBJ_description         OBJ_X509,13L

#define LN_searchGuide          "searchGuide"
#define NID_searchGuide         859
#define OBJ_searchGuide         OBJ_X509,14L

#define LN_businessCategory             "businessCategory"
#define NID_businessCategory            860
#define OBJ_businessCategory            OBJ_X509,15L

#define LN_postalAddress                "postalAddress"
#define NID_postalAddress               861
#define OBJ_postalAddress               OBJ_X509,16L

#define LN_postalCode           "postalCode"
#define NID_postalCode          661
#define OBJ_postalCode          OBJ_X509,17L

#define LN_postOfficeBox                "postOfficeBox"
#define NID_postOfficeBox               862
#define OBJ_postOfficeBox               OBJ_X509,18L

#define LN_physicalDeliveryOfficeName           "physicalDeliveryOfficeName"
#define NID_physicalDeliveryOfficeName          863
#define OBJ_physicalDeliveryOfficeName          OBJ_X509,19L

#define LN_telephoneNumber              "telephoneNumber"
#define NID_telephoneNumber             864
#define OBJ_telephoneNumber             OBJ_X509,20L

#define LN_telexNumber          "telexNumber"
#define NID_telexNumber         865
#define OBJ_telexNumber         OBJ_X509,21L

#define LN_teletexTerminalIdentifier            "teletexTerminalIdentifier"
#define NID_teletexTerminalIdentifier           866
#define OBJ_teletexTerminalIdentifier           OBJ_X509,22L

#define LN_facsimileTelephoneNumber             "facsimileTelephoneNumber"
#define NID_facsimileTelephoneNumber            867
#define OBJ_facsimileTelephoneNumber            OBJ_X509,23L

#define LN_x121Address          "x121Address"
#define NID_x121Address         868
#define OBJ_x121Address         OBJ_X509,24L

#define LN_internationaliSDNNumber              "internationaliSDNNumber"
#define NID_internationaliSDNNumber             869
#define OBJ_internationaliSDNNumber             OBJ_X509,25L

#define LN_registeredAddress            "registeredAddress"
#define NID_registeredAddress           870
#define OBJ_registeredAddress           OBJ_X509,26L

#define LN_destinationIndicator         "destinationIndicator"
#define NID_destinationIndicator                871
#define OBJ_destinationIndicator                OBJ_X509,27L

#define LN_preferredDeliveryMethod              "preferredDeliveryMethod"
#define NID_preferredDeliveryMethod             872
#define OBJ_preferredDeliveryMethod             OBJ_X509,28L

#define LN_presentationAddress          "presentationAddress"
#define NID_presentationAddress         873
#define OBJ_presentationAddress         OBJ_X509,29L

#define LN_supportedApplicationContext          "supportedApplicationContext"
#define NID_supportedApplicationContext         874
#define OBJ_supportedApplicationContext         OBJ_X509,30L

#define SN_member               "member"
#define NID_member              875
#define OBJ_member              OBJ_X509,31L

#define SN_owner                "owner"
#define NID_owner               876
#define OBJ_owner               OBJ_X509,32L

#define LN_roleOccupant         "roleOccupant"
#define NID_roleOccupant                877
#define OBJ_roleOccupant                OBJ_X509,33L

#define SN_seeAlso              "seeAlso"
#define NID_seeAlso             878
#define OBJ_seeAlso             OBJ_X509,34L

#define LN_userPassword         "userPassword"
#define NID_userPassword                879
#define OBJ_userPassword                OBJ_X509,35L

#define LN_userCertificate              "userCertificate"
#define NID_userCertificate             880
#define OBJ_userCertificate             OBJ_X509,36L

#define LN_cACertificate                "cACertificate"
#define NID_cACertificate               881
#define OBJ_cACertificate               OBJ_X509,37L

#define LN_authorityRevocationList              "authorityRevocationList"
#define NID_authorityRevocationList             882
#define OBJ_authorityRevocationList             OBJ_X509,38L

#define LN_certificateRevocationList            "certificateRevocationList"
#define NID_certificateRevocationList           883
#define OBJ_certificateRevocationList           OBJ_X509,39L

#define LN_crossCertificatePair         "crossCertificatePair"
#define NID_crossCertificatePair                884
#define OBJ_crossCertificatePair                OBJ_X509,40L

#define SN_name         "name"
#define LN_name         "name"
#define NID_name                173
#define OBJ_name                OBJ_X509,41L

#define SN_givenName            "GN"
#define LN_givenName            "givenName"
#define NID_givenName           99
#define OBJ_givenName           OBJ_X509,42L

#define SN_initials             "initials"
#define LN_initials             "initials"
#define NID_initials            101
#define OBJ_initials            OBJ_X509,43L

#define LN_generationQualifier          "generationQualifier"
#define NID_generationQualifier         509
#define OBJ_generationQualifier         OBJ_X509,44L

#define LN_x500UniqueIdentifier         "x500UniqueIdentifier"
#define NID_x500UniqueIdentifier                503
#define OBJ_x500UniqueIdentifier                OBJ_X509,45L

#define SN_dnQualifier          "dnQualifier"
#define LN_dnQualifier          "dnQualifier"
#define NID_dnQualifier         174
#define OBJ_dnQualifier         OBJ_X509,46L

#define LN_enhancedSearchGuide          "enhancedSearchGuide"
#define NID_enhancedSearchGuide         885
#define OBJ_enhancedSearchGuide         OBJ_X509,47L

#define LN_protocolInformation          "protocolInformation"
#define NID_protocolInformation         886
#define OBJ_protocolInformation         OBJ_X509,48L

#define LN_distinguishedName            "distinguishedName"
#define NID_distinguishedName           887
#define OBJ_distinguishedName           OBJ_X509,49L

#define LN_uniqueMember         "uniqueMember"
#define NID_uniqueMember                888
#define OBJ_uniqueMember                OBJ_X509,50L

#define LN_houseIdentifier              "houseIdentifier"
#define NID_houseIdentifier             889
#define OBJ_houseIdentifier             OBJ_X509,51L

#define LN_supportedAlgorithms          "supportedAlgorithms"
#define NID_supportedAlgorithms         890
#define OBJ_supportedAlgorithms         OBJ_X509,52L

#define LN_deltaRevocationList          "deltaRevocationList"
#define NID_deltaRevocationList         891
#define OBJ_deltaRevocationList         OBJ_X509,53L

#define SN_dmdName              "dmdName"
#define NID_dmdName             892
#define OBJ_dmdName             OBJ_X509,54L

#define LN_pseudonym            "pseudonym"
#define NID_pseudonym           510
#define OBJ_pseudonym           OBJ_X509,65L

#define SN_role         "role"
#define LN_role         "role"
#define NID_role                400
#define OBJ_role                OBJ_X509,72L

#define SN_X500algorithms               "X500algorithms"
#define LN_X500algorithms               "directory services - algorithms"
#define NID_X500algorithms              378
#define OBJ_X500algorithms              OBJ_X500,8L

#define SN_rsa          "RSA"
#define LN_rsa          "rsa"
#define NID_rsa         19
#define OBJ_rsa         OBJ_X500algorithms,1L,1L

#define SN_mdc2WithRSA          "RSA-MDC2"
#define LN_mdc2WithRSA          "mdc2WithRSA"
#define NID_mdc2WithRSA         96
#define OBJ_mdc2WithRSA         OBJ_X500algorithms,3L,100L

#define SN_mdc2         "MDC2"
#define LN_mdc2         "mdc2"
#define NID_mdc2                95
#define OBJ_mdc2                OBJ_X500algorithms,3L,101L

#define SN_id_ce                "id-ce"
#define NID_id_ce               81
#define OBJ_id_ce               OBJ_X500,29L

#define SN_subject_directory_attributes         "subjectDirectoryAttributes"
#define LN_subject_directory_attributes         "X509v3 Subject Directory Attributes"
#define NID_subject_directory_attributes                769
#define OBJ_subject_directory_attributes                OBJ_id_ce,9L

#define SN_subject_key_identifier               "subjectKeyIdentifier"
#define LN_subject_key_identifier               "X509v3 Subject Key Identifier"
#define NID_subject_key_identifier              82
#define OBJ_subject_key_identifier              OBJ_id_ce,14L

#define SN_key_usage            "keyUsage"
#define LN_key_usage            "X509v3 Key Usage"
#define NID_key_usage           83
#define OBJ_key_usage           OBJ_id_ce,15L

#define SN_private_key_usage_period             "privateKeyUsagePeriod"
#define LN_private_key_usage_period             "X509v3 Private Key Usage Period"
#define NID_private_key_usage_period            84
#define OBJ_private_key_usage_period            OBJ_id_ce,16L

#define SN_subject_alt_name             "subjectAltName"
#define LN_subject_alt_name             "X509v3 Subject Alternative Name"
#define NID_subject_alt_name            85
#define OBJ_subject_alt_name            OBJ_id_ce,17L

#define SN_issuer_alt_name              "issuerAltName"
#define LN_issuer_alt_name              "X509v3 Issuer Alternative Name"
#define NID_issuer_alt_name             86
#define OBJ_issuer_alt_name             OBJ_id_ce,18L

#define SN_basic_constraints            "basicConstraints"
#define LN_basic_constraints            "X509v3 Basic Constraints"
#define NID_basic_constraints           87
#define OBJ_basic_constraints           OBJ_id_ce,19L

#define SN_crl_number           "crlNumber"
#define LN_crl_number           "X509v3 CRL Number"
#define NID_crl_number          88
#define OBJ_crl_number          OBJ_id_ce,20L

#define SN_crl_reason           "CRLReason"
#define LN_crl_reason           "X509v3 CRL Reason Code"
#define NID_crl_reason          141
#define OBJ_crl_reason          OBJ_id_ce,21L

#define SN_invalidity_date              "invalidityDate"
#define LN_invalidity_date              "Invalidity Date"
#define NID_invalidity_date             142
#define OBJ_invalidity_date             OBJ_id_ce,24L

#define SN_delta_crl            "deltaCRL"
#define LN_delta_crl            "X509v3 Delta CRL Indicator"
#define NID_delta_crl           140
#define OBJ_delta_crl           OBJ_id_ce,27L

#define SN_issuing_distribution_point           "issuingDistributionPoint"
#define LN_issuing_distribution_point           "X509v3 Issuing Distrubution Point"
#define NID_issuing_distribution_point          770
#define OBJ_issuing_distribution_point          OBJ_id_ce,28L

#define SN_certificate_issuer           "certificateIssuer"
#define LN_certificate_issuer           "X509v3 Certificate Issuer"
#define NID_certificate_issuer          771
#define OBJ_certificate_issuer          OBJ_id_ce,29L

#define SN_name_constraints             "nameConstraints"
#define LN_name_constraints             "X509v3 Name Constraints"
#define NID_name_constraints            666
#define OBJ_name_constraints            OBJ_id_ce,30L

#define SN_crl_distribution_points              "crlDistributionPoints"
#define LN_crl_distribution_points              "X509v3 CRL Distribution Points"
#define NID_crl_distribution_points             103
#define OBJ_crl_distribution_points             OBJ_id_ce,31L

#define SN_certificate_policies         "certificatePolicies"
#define LN_certificate_policies         "X509v3 Certificate Policies"
#define NID_certificate_policies                89
#define OBJ_certificate_policies                OBJ_id_ce,32L

#define SN_any_policy           "anyPolicy"
#define LN_any_policy           "X509v3 Any Policy"
#define NID_any_policy          746
#define OBJ_any_policy          OBJ_certificate_policies,0L

#define SN_policy_mappings              "policyMappings"
#define LN_policy_mappings              "X509v3 Policy Mappings"
#define NID_policy_mappings             747
#define OBJ_policy_mappings             OBJ_id_ce,33L

#define SN_authority_key_identifier             "authorityKeyIdentifier"
#define LN_authority_key_identifier             "X509v3 Authority Key Identifier"
#define NID_authority_key_identifier            90
#define OBJ_authority_key_identifier            OBJ_id_ce,35L

#define SN_policy_constraints           "policyConstraints"
#define LN_policy_constraints           "X509v3 Policy Constraints"
#define NID_policy_constraints          401
#define OBJ_policy_constraints          OBJ_id_ce,36L

#define SN_ext_key_usage                "extendedKeyUsage"
#define LN_ext_key_usage                "X509v3 Extended Key Usage"
#define NID_ext_key_usage               126
#define OBJ_ext_key_usage               OBJ_id_ce,37L

#define SN_freshest_crl         "freshestCRL"
#define LN_freshest_crl         "X509v3 Freshest CRL"
#define NID_freshest_crl                857
#define OBJ_freshest_crl                OBJ_id_ce,46L

#define SN_inhibit_any_policy           "inhibitAnyPolicy"
#define LN_inhibit_any_policy           "X509v3 Inhibit Any Policy"
#define NID_inhibit_any_policy          748
#define OBJ_inhibit_any_policy          OBJ_id_ce,54L

#define SN_target_information           "targetInformation"
#define LN_target_information           "X509v3 AC Targeting"
#define NID_target_information          402
#define OBJ_target_information          OBJ_id_ce,55L

#define SN_no_rev_avail         "noRevAvail"
#define LN_no_rev_avail         "X509v3 No Revocation Available"
#define NID_no_rev_avail                403
#define OBJ_no_rev_avail                OBJ_id_ce,56L

#define SN_anyExtendedKeyUsage          "anyExtendedKeyUsage"
#define LN_anyExtendedKeyUsage          "Any Extended Key Usage"
#define NID_anyExtendedKeyUsage         910
#define OBJ_anyExtendedKeyUsage         OBJ_ext_key_usage,0L

#define SN_netscape             "Netscape"
#define LN_netscape             "Netscape Communications Corp."
#define NID_netscape            57
#define OBJ_netscape            2L,16L,840L,1L,113730L

#define SN_netscape_cert_extension              "nsCertExt"
#define LN_netscape_cert_extension              "Netscape Certificate Extension"
#define NID_netscape_cert_extension             58
#define OBJ_netscape_cert_extension             OBJ_netscape,1L

#define SN_netscape_data_type           "nsDataType"
#define LN_netscape_data_type           "Netscape Data Type"
#define NID_netscape_data_type          59
#define OBJ_netscape_data_type          OBJ_netscape,2L

#define SN_netscape_cert_type           "nsCertType"
#define LN_netscape_cert_type           "Netscape Cert Type"
#define NID_netscape_cert_type          71
#define OBJ_netscape_cert_type          OBJ_netscape_cert_extension,1L

#define SN_netscape_base_url            "nsBaseUrl"
#define LN_netscape_base_url            "Netscape Base Url"
#define NID_netscape_base_url           72
#define OBJ_netscape_base_url           OBJ_netscape_cert_extension,2L

#define SN_netscape_revocation_url              "nsRevocationUrl"
#define LN_netscape_revocation_url              "Netscape Revocation Url"
#define NID_netscape_revocation_url             73
#define OBJ_netscape_revocation_url             OBJ_netscape_cert_extension,3L

#define SN_netscape_ca_revocation_url           "nsCaRevocationUrl"
#define LN_netscape_ca_revocation_url           "Netscape CA Revocation Url"
#define NID_netscape_ca_revocation_url          74
#define OBJ_netscape_ca_revocation_url          OBJ_netscape_cert_extension,4L

#define SN_netscape_renewal_url         "nsRenewalUrl"
#define LN_netscape_renewal_url         "Netscape Renewal Url"
#define NID_netscape_renewal_url                75
#define OBJ_netscape_renewal_url                OBJ_netscape_cert_extension,7L

#define SN_netscape_ca_policy_url               "nsCaPolicyUrl"
#define LN_netscape_ca_policy_url               "Netscape CA Policy Url"
#define NID_netscape_ca_policy_url              76
#define OBJ_netscape_ca_policy_url              OBJ_netscape_cert_extension,8L

#define SN_netscape_ssl_server_name             "nsSslServerName"
#define LN_netscape_ssl_server_name             "Netscape SSL Server Name"
#define NID_netscape_ssl_server_name            77
#define OBJ_netscape_ssl_server_name            OBJ_netscape_cert_extension,12L

#define SN_netscape_comment             "nsComment"
#define LN_netscape_comment             "Netscape Comment"
#define NID_netscape_comment            78
#define OBJ_netscape_comment            OBJ_netscape_cert_extension,13L

#define SN_netscape_cert_sequence               "nsCertSequence"
#define LN_netscape_cert_sequence               "Netscape Certificate Sequence"
#define NID_netscape_cert_sequence              79
#define OBJ_netscape_cert_sequence              OBJ_netscape_data_type,5L

#define SN_ns_sgc               "nsSGC"
#define LN_ns_sgc               "Netscape Server Gated Crypto"
#define NID_ns_sgc              139
#define OBJ_ns_sgc              OBJ_netscape,4L,1L

#define SN_org          "ORG"
#define LN_org          "org"
#define NID_org         379
#define OBJ_org         OBJ_iso,3L

#define SN_dod          "DOD"
#define LN_dod          "dod"
#define NID_dod         380
#define OBJ_dod         OBJ_org,6L

#define SN_iana         "IANA"
#define LN_iana         "iana"
#define NID_iana                381
#define OBJ_iana                OBJ_dod,1L

#define OBJ_internet            OBJ_iana

#define SN_Directory            "directory"
#define LN_Directory            "Directory"
#define NID_Directory           382
#define OBJ_Directory           OBJ_internet,1L

#define SN_Management           "mgmt"
#define LN_Management           "Management"
#define NID_Management          383
#define OBJ_Management          OBJ_internet,2L

#define SN_Experimental         "experimental"
#define LN_Experimental         "Experimental"
#define NID_Experimental                384
#define OBJ_Experimental                OBJ_internet,3L

#define SN_Private              "private"
#define LN_Private              "Private"
#define NID_Private             385
#define OBJ_Private             OBJ_internet,4L

#define SN_Security             "security"
#define LN_Security             "Security"
#define NID_Security            386
#define OBJ_Security            OBJ_internet,5L

#define SN_SNMPv2               "snmpv2"
#define LN_SNMPv2               "SNMPv2"
#define NID_SNMPv2              387
#define OBJ_SNMPv2              OBJ_internet,6L

#define LN_Mail         "Mail"
#define NID_Mail                388
#define OBJ_Mail                OBJ_internet,7L

#define SN_Enterprises          "enterprises"
#define LN_Enterprises          "Enterprises"
#define NID_Enterprises         389
#define OBJ_Enterprises         OBJ_Private,1L

#define SN_dcObject             "dcobject"
#define LN_dcObject             "dcObject"
#define NID_dcObject            390
#define OBJ_dcObject            OBJ_Enterprises,1466L,344L

#define SN_mime_mhs             "mime-mhs"
#define LN_mime_mhs             "MIME MHS"
#define NID_mime_mhs            504
#define OBJ_mime_mhs            OBJ_Mail,1L

#define SN_mime_mhs_headings            "mime-mhs-headings"
#define LN_mime_mhs_headings            "mime-mhs-headings"
#define NID_mime_mhs_headings           505
#define OBJ_mime_mhs_headings           OBJ_mime_mhs,1L

#define SN_mime_mhs_bodies              "mime-mhs-bodies"
#define LN_mime_mhs_bodies              "mime-mhs-bodies"
#define NID_mime_mhs_bodies             506
#define OBJ_mime_mhs_bodies             OBJ_mime_mhs,2L

#define SN_id_hex_partial_message               "id-hex-partial-message"
#define LN_id_hex_partial_message               "id-hex-partial-message"
#define NID_id_hex_partial_message              507
#define OBJ_id_hex_partial_message              OBJ_mime_mhs_headings,1L

#define SN_id_hex_multipart_message             "id-hex-multipart-message"
#define LN_id_hex_multipart_message             "id-hex-multipart-message"
#define NID_id_hex_multipart_message            508
#define OBJ_id_hex_multipart_message            OBJ_mime_mhs_headings,2L

#define SN_rle_compression              "RLE"
#define LN_rle_compression              "run length compression"
#define NID_rle_compression             124
#define OBJ_rle_compression             1L,1L,1L,1L,666L,1L

#define SN_zlib_compression             "ZLIB"
#define LN_zlib_compression             "zlib compression"
#define NID_zlib_compression            125
#define OBJ_zlib_compression            OBJ_id_smime_alg,8L

#define OBJ_csor                2L,16L,840L,1L,101L,3L

#define OBJ_nistAlgorithms              OBJ_csor,4L

#define OBJ_aes         OBJ_nistAlgorithms,1L

#define SN_aes_128_ecb          "AES-128-ECB"
#define LN_aes_128_ecb          "aes-128-ecb"
#define NID_aes_128_ecb         418
#define OBJ_aes_128_ecb         OBJ_aes,1L

#define SN_aes_128_cbc          "AES-128-CBC"
#define LN_aes_128_cbc          "aes-128-cbc"
#define NID_aes_128_cbc         419
#define OBJ_aes_128_cbc         OBJ_aes,2L

#define SN_aes_128_ofb128               "AES-128-OFB"
#define LN_aes_128_ofb128               "aes-128-ofb"
#define NID_aes_128_ofb128              420
#define OBJ_aes_128_ofb128              OBJ_aes,3L

#define SN_aes_128_cfb128               "AES-128-CFB"
#define LN_aes_128_cfb128               "aes-128-cfb"
#define NID_aes_128_cfb128              421
#define OBJ_aes_128_cfb128              OBJ_aes,4L

#define SN_id_aes128_wrap               "id-aes128-wrap"
#define NID_id_aes128_wrap              788
#define OBJ_id_aes128_wrap              OBJ_aes,5L

#define SN_aes_128_gcm          "id-aes128-GCM"
#define LN_aes_128_gcm          "aes-128-gcm"
#define NID_aes_128_gcm         895
#define OBJ_aes_128_gcm         OBJ_aes,6L

#define SN_aes_128_ccm          "id-aes128-CCM"
#define LN_aes_128_ccm          "aes-128-ccm"
#define NID_aes_128_ccm         896
#define OBJ_aes_128_ccm         OBJ_aes,7L

#define SN_id_aes128_wrap_pad           "id-aes128-wrap-pad"
#define NID_id_aes128_wrap_pad          897
#define OBJ_id_aes128_wrap_pad          OBJ_aes,8L

#define SN_aes_192_ecb          "AES-192-ECB"
#define LN_aes_192_ecb          "aes-192-ecb"
#define NID_aes_192_ecb         422
#define OBJ_aes_192_ecb         OBJ_aes,21L

#define SN_aes_192_cbc          "AES-192-CBC"
#define LN_aes_192_cbc          "aes-192-cbc"
#define NID_aes_192_cbc         423
#define OBJ_aes_192_cbc         OBJ_aes,22L

#define SN_aes_192_ofb128               "AES-192-OFB"
#define LN_aes_192_ofb128               "aes-192-ofb"
#define NID_aes_192_ofb128              424
#define OBJ_aes_192_ofb128              OBJ_aes,23L

#define SN_aes_192_cfb128               "AES-192-CFB"
#define LN_aes_192_cfb128               "aes-192-cfb"
#define NID_aes_192_cfb128              425
#define OBJ_aes_192_cfb128              OBJ_aes,24L

#define SN_id_aes192_wrap               "id-aes192-wrap"
#define NID_id_aes192_wrap              789
#define OBJ_id_aes192_wrap              OBJ_aes,25L

#define SN_aes_192_gcm          "id-aes192-GCM"
#define LN_aes_192_gcm          "aes-192-gcm"
#define NID_aes_192_gcm         898
#define OBJ_aes_192_gcm         OBJ_aes,26L

#define SN_aes_192_ccm          "id-aes192-CCM"
#define LN_aes_192_ccm          "aes-192-ccm"
#define NID_aes_192_ccm         899
#define OBJ_aes_192_ccm         OBJ_aes,27L

#define SN_id_aes192_wrap_pad           "id-aes192-wrap-pad"
#define NID_id_aes192_wrap_pad          900
#define OBJ_id_aes192_wrap_pad          OBJ_aes,28L

#define SN_aes_256_ecb          "AES-256-ECB"
#define LN_aes_256_ecb          "aes-256-ecb"
#define NID_aes_256_ecb         426
#define OBJ_aes_256_ecb         OBJ_aes,41L

#define SN_aes_256_cbc          "AES-256-CBC"
#define LN_aes_256_cbc          "aes-256-cbc"
#define NID_aes_256_cbc         427
#define OBJ_aes_256_cbc         OBJ_aes,42L

#define SN_aes_256_ofb128               "AES-256-OFB"
#define LN_aes_256_ofb128               "aes-256-ofb"
#define NID_aes_256_ofb128              428
#define OBJ_aes_256_ofb128              OBJ_aes,43L

#define SN_aes_256_cfb128               "AES-256-CFB"
#define LN_aes_256_cfb128               "aes-256-cfb"
#define NID_aes_256_cfb128              429
#define OBJ_aes_256_cfb128              OBJ_aes,44L

#define SN_id_aes256_wrap               "id-aes256-wrap"
#define NID_id_aes256_wrap              790
#define OBJ_id_aes256_wrap              OBJ_aes,45L

#define SN_aes_256_gcm          "id-aes256-GCM"
#define LN_aes_256_gcm          "aes-256-gcm"
#define NID_aes_256_gcm         901
#define OBJ_aes_256_gcm         OBJ_aes,46L

#define SN_aes_256_ccm          "id-aes256-CCM"
#define LN_aes_256_ccm          "aes-256-ccm"
#define NID_aes_256_ccm         902
#define OBJ_aes_256_ccm         OBJ_aes,47L

#define SN_id_aes256_wrap_pad           "id-aes256-wrap-pad"
#define NID_id_aes256_wrap_pad          903
#define OBJ_id_aes256_wrap_pad          OBJ_aes,48L

#define SN_aes_128_cfb1         "AES-128-CFB1"
#define LN_aes_128_cfb1         "aes-128-cfb1"
#define NID_aes_128_cfb1                650

#define SN_aes_192_cfb1         "AES-192-CFB1"
#define LN_aes_192_cfb1         "aes-192-cfb1"
#define NID_aes_192_cfb1                651

#define SN_aes_256_cfb1         "AES-256-CFB1"
#define LN_aes_256_cfb1         "aes-256-cfb1"
#define NID_aes_256_cfb1                652

#define SN_aes_128_cfb8         "AES-128-CFB8"
#define LN_aes_128_cfb8         "aes-128-cfb8"
#define NID_aes_128_cfb8                653

#define SN_aes_192_cfb8         "AES-192-CFB8"
#define LN_aes_192_cfb8         "aes-192-cfb8"
#define NID_aes_192_cfb8                654

#define SN_aes_256_cfb8         "AES-256-CFB8"
#define LN_aes_256_cfb8         "aes-256-cfb8"
#define NID_aes_256_cfb8                655

#define SN_aes_128_ctr          "AES-128-CTR"
#define LN_aes_128_ctr          "aes-128-ctr"
#define NID_aes_128_ctr         904

#define SN_aes_192_ctr          "AES-192-CTR"
#define LN_aes_192_ctr          "aes-192-ctr"
#define NID_aes_192_ctr         905

#define SN_aes_256_ctr          "AES-256-CTR"
#define LN_aes_256_ctr          "aes-256-ctr"
#define NID_aes_256_ctr         906

#define SN_aes_128_xts          "AES-128-XTS"
#define LN_aes_128_xts          "aes-128-xts"
#define NID_aes_128_xts         913

#define SN_aes_256_xts          "AES-256-XTS"
#define LN_aes_256_xts          "aes-256-xts"
#define NID_aes_256_xts         914

#define SN_des_cfb1             "DES-CFB1"
#define LN_des_cfb1             "des-cfb1"
#define NID_des_cfb1            656

#define SN_des_cfb8             "DES-CFB8"
#define LN_des_cfb8             "des-cfb8"
#define NID_des_cfb8            657

#define SN_des_ede3_cfb1                "DES-EDE3-CFB1"
#define LN_des_ede3_cfb1                "des-ede3-cfb1"
#define NID_des_ede3_cfb1               658

#define SN_des_ede3_cfb8                "DES-EDE3-CFB8"
#define LN_des_ede3_cfb8                "des-ede3-cfb8"
#define NID_des_ede3_cfb8               659

#define OBJ_nist_hashalgs               OBJ_nistAlgorithms,2L

#define SN_sha256               "SHA256"
#define LN_sha256               "sha256"
#define NID_sha256              672
#define OBJ_sha256              OBJ_nist_hashalgs,1L

#define SN_sha384               "SHA384"
#define LN_sha384               "sha384"
#define NID_sha384              673
#define OBJ_sha384              OBJ_nist_hashalgs,2L

#define SN_sha512               "SHA512"
#define LN_sha512               "sha512"
#define NID_sha512              674
#define OBJ_sha512              OBJ_nist_hashalgs,3L

#define SN_sha224               "SHA224"
#define LN_sha224               "sha224"
#define NID_sha224              675
#define OBJ_sha224              OBJ_nist_hashalgs,4L

#define OBJ_dsa_with_sha2               OBJ_nistAlgorithms,3L

#define SN_dsa_with_SHA224              "dsa_with_SHA224"
#define NID_dsa_with_SHA224             802
#define OBJ_dsa_with_SHA224             OBJ_dsa_with_sha2,1L

#define SN_dsa_with_SHA256              "dsa_with_SHA256"
#define NID_dsa_with_SHA256             803
#define OBJ_dsa_with_SHA256             OBJ_dsa_with_sha2,2L

#define SN_hold_instruction_code                "holdInstructionCode"
#define LN_hold_instruction_code                "Hold Instruction Code"
#define NID_hold_instruction_code               430
#define OBJ_hold_instruction_code               OBJ_id_ce,23L

#define OBJ_holdInstruction             OBJ_X9_57,2L

#define SN_hold_instruction_none                "holdInstructionNone"
#define LN_hold_instruction_none                "Hold Instruction None"
#define NID_hold_instruction_none               431
#define OBJ_hold_instruction_none               OBJ_holdInstruction,1L

#define SN_hold_instruction_call_issuer         "holdInstructionCallIssuer"
#define LN_hold_instruction_call_issuer         "Hold Instruction Call Issuer"
#define NID_hold_instruction_call_issuer                432
#define OBJ_hold_instruction_call_issuer                OBJ_holdInstruction,2L

#define SN_hold_instruction_reject              "holdInstructionReject"
#define LN_hold_instruction_reject              "Hold Instruction Reject"
#define NID_hold_instruction_reject             433
#define OBJ_hold_instruction_reject             OBJ_holdInstruction,3L

#define SN_data         "data"
#define NID_data                434
#define OBJ_data                OBJ_itu_t,9L

#define SN_pss          "pss"
#define NID_pss         435
#define OBJ_pss         OBJ_data,2342L

#define SN_ucl          "ucl"
#define NID_ucl         436
#define OBJ_ucl         OBJ_pss,19200300L

#define SN_pilot                "pilot"
#define NID_pilot               437
#define OBJ_pilot               OBJ_ucl,100L

#define LN_pilotAttributeType           "pilotAttributeType"
#define NID_pilotAttributeType          438
#define OBJ_pilotAttributeType          OBJ_pilot,1L

#define LN_pilotAttributeSyntax         "pilotAttributeSyntax"
#define NID_pilotAttributeSyntax                439
#define OBJ_pilotAttributeSyntax                OBJ_pilot,3L

#define LN_pilotObjectClass             "pilotObjectClass"
#define NID_pilotObjectClass            440
#define OBJ_pilotObjectClass            OBJ_pilot,4L

#define LN_pilotGroups          "pilotGroups"
#define NID_pilotGroups         441
#define OBJ_pilotGroups         OBJ_pilot,10L

#define LN_iA5StringSyntax              "iA5StringSyntax"
#define NID_iA5StringSyntax             442
#define OBJ_iA5StringSyntax             OBJ_pilotAttributeSyntax,4L

#define LN_caseIgnoreIA5StringSyntax            "caseIgnoreIA5StringSyntax"
#define NID_caseIgnoreIA5StringSyntax           443
#define OBJ_caseIgnoreIA5StringSyntax           OBJ_pilotAttributeSyntax,5L

#define LN_pilotObject          "pilotObject"
#define NID_pilotObject         444
#define OBJ_pilotObject         OBJ_pilotObjectClass,3L

#define LN_pilotPerson          "pilotPerson"
#define NID_pilotPerson         445
#define OBJ_pilotPerson         OBJ_pilotObjectClass,4L

#define SN_account              "account"
#define NID_account             446
#define OBJ_account             OBJ_pilotObjectClass,5L

#define SN_document             "document"
#define NID_document            447
#define OBJ_document            OBJ_pilotObjectClass,6L

#define SN_room         "room"
#define NID_room                448
#define OBJ_room                OBJ_pilotObjectClass,7L

#define LN_documentSeries               "documentSeries"
#define NID_documentSeries              449
#define OBJ_documentSeries              OBJ_pilotObjectClass,9L

#define SN_Domain               "domain"
#define LN_Domain               "Domain"
#define NID_Domain              392
#define OBJ_Domain              OBJ_pilotObjectClass,13L

#define LN_rFC822localPart              "rFC822localPart"
#define NID_rFC822localPart             450
#define OBJ_rFC822localPart             OBJ_pilotObjectClass,14L

#define LN_dNSDomain            "dNSDomain"
#define NID_dNSDomain           451
#define OBJ_dNSDomain           OBJ_pilotObjectClass,15L

#define LN_domainRelatedObject          "domainRelatedObject"
#define NID_domainRelatedObject         452
#define OBJ_domainRelatedObject         OBJ_pilotObjectClass,17L

#define LN_friendlyCountry              "friendlyCountry"
#define NID_friendlyCountry             453
#define OBJ_friendlyCountry             OBJ_pilotObjectClass,18L

#define LN_simpleSecurityObject         "simpleSecurityObject"
#define NID_simpleSecurityObject                454
#define OBJ_simpleSecurityObject                OBJ_pilotObjectClass,19L

#define LN_pilotOrganization            "pilotOrganization"
#define NID_pilotOrganization           455
#define OBJ_pilotOrganization           OBJ_pilotObjectClass,20L

#define LN_pilotDSA             "pilotDSA"
#define NID_pilotDSA            456
#define OBJ_pilotDSA            OBJ_pilotObjectClass,21L

#define LN_qualityLabelledData          "qualityLabelledData"
#define NID_qualityLabelledData         457
#define OBJ_qualityLabelledData         OBJ_pilotObjectClass,22L

#define SN_userId               "UID"
#define LN_userId               "userId"
#define NID_userId              458
#define OBJ_userId              OBJ_pilotAttributeType,1L

#define LN_textEncodedORAddress         "textEncodedORAddress"
#define NID_textEncodedORAddress                459
#define OBJ_textEncodedORAddress                OBJ_pilotAttributeType,2L

#define SN_rfc822Mailbox                "mail"
#define LN_rfc822Mailbox                "rfc822Mailbox"
#define NID_rfc822Mailbox               460
#define OBJ_rfc822Mailbox               OBJ_pilotAttributeType,3L

#define SN_info         "info"
#define NID_info                461
#define OBJ_info                OBJ_pilotAttributeType,4L

#define LN_favouriteDrink               "favouriteDrink"
#define NID_favouriteDrink              462
#define OBJ_favouriteDrink              OBJ_pilotAttributeType,5L

#define LN_roomNumber           "roomNumber"
#define NID_roomNumber          463
#define OBJ_roomNumber          OBJ_pilotAttributeType,6L

#define SN_photo                "photo"
#define NID_photo               464
#define OBJ_photo               OBJ_pilotAttributeType,7L

#define LN_userClass            "userClass"
#define NID_userClass           465
#define OBJ_userClass           OBJ_pilotAttributeType,8L

#define SN_host         "host"
#define NID_host                466
#define OBJ_host                OBJ_pilotAttributeType,9L

#define SN_manager              "manager"
#define NID_manager             467
#define OBJ_manager             OBJ_pilotAttributeType,10L

#define LN_documentIdentifier           "documentIdentifier"
#define NID_documentIdentifier          468
#define OBJ_documentIdentifier          OBJ_pilotAttributeType,11L

#define LN_documentTitle                "documentTitle"
#define NID_documentTitle               469
#define OBJ_documentTitle               OBJ_pilotAttributeType,12L

#define LN_documentVersion              "documentVersion"
#define NID_documentVersion             470
#define OBJ_documentVersion             OBJ_pilotAttributeType,13L

#define LN_documentAuthor               "documentAuthor"
#define NID_documentAuthor              471
#define OBJ_documentAuthor              OBJ_pilotAttributeType,14L

#define LN_documentLocation             "documentLocation"
#define NID_documentLocation            472
#define OBJ_documentLocation            OBJ_pilotAttributeType,15L

#define LN_homeTelephoneNumber          "homeTelephoneNumber"
#define NID_homeTelephoneNumber         473
#define OBJ_homeTelephoneNumber         OBJ_pilotAttributeType,20L

#define SN_secretary            "secretary"
#define NID_secretary           474
#define OBJ_secretary           OBJ_pilotAttributeType,21L

#define LN_otherMailbox         "otherMailbox"
#define NID_otherMailbox                475
#define OBJ_otherMailbox                OBJ_pilotAttributeType,22L

#define LN_lastModifiedTime             "lastModifiedTime"
#define NID_lastModifiedTime            476
#define OBJ_lastModifiedTime            OBJ_pilotAttributeType,23L

#define LN_lastModifiedBy               "lastModifiedBy"
#define NID_lastModifiedBy              477
#define OBJ_lastModifiedBy              OBJ_pilotAttributeType,24L

#define SN_domainComponent              "DC"
#define LN_domainComponent              "domainComponent"
#define NID_domainComponent             391
#define OBJ_domainComponent             OBJ_pilotAttributeType,25L

#define LN_aRecord              "aRecord"
#define NID_aRecord             478
#define OBJ_aRecord             OBJ_pilotAttributeType,26L

#define LN_pilotAttributeType27         "pilotAttributeType27"
#define NID_pilotAttributeType27                479
#define OBJ_pilotAttributeType27                OBJ_pilotAttributeType,27L

#define LN_mXRecord             "mXRecord"
#define NID_mXRecord            480
#define OBJ_mXRecord            OBJ_pilotAttributeType,28L

#define LN_nSRecord             "nSRecord"
#define NID_nSRecord            481
#define OBJ_nSRecord            OBJ_pilotAttributeType,29L

#define LN_sOARecord            "sOARecord"
#define NID_sOARecord           482
#define OBJ_sOARecord           OBJ_pilotAttributeType,30L

#define LN_cNAMERecord          "cNAMERecord"
#define NID_cNAMERecord         483
#define OBJ_cNAMERecord         OBJ_pilotAttributeType,31L

#define LN_associatedDomain             "associatedDomain"
#define NID_associatedDomain            484
#define OBJ_associatedDomain            OBJ_pilotAttributeType,37L

#define LN_associatedName               "associatedName"
#define NID_associatedName              485
#define OBJ_associatedName              OBJ_pilotAttributeType,38L

#define LN_homePostalAddress            "homePostalAddress"
#define NID_homePostalAddress           486
#define OBJ_homePostalAddress           OBJ_pilotAttributeType,39L

#define LN_personalTitle                "personalTitle"
#define NID_personalTitle               487
#define OBJ_personalTitle               OBJ_pilotAttributeType,40L

#define LN_mobileTelephoneNumber                "mobileTelephoneNumber"
#define NID_mobileTelephoneNumber               488
#define OBJ_mobileTelephoneNumber               OBJ_pilotAttributeType,41L

#define LN_pagerTelephoneNumber         "pagerTelephoneNumber"
#define NID_pagerTelephoneNumber                489
#define OBJ_pagerTelephoneNumber                OBJ_pilotAttributeType,42L

#define LN_friendlyCountryName          "friendlyCountryName"
#define NID_friendlyCountryName         490
#define OBJ_friendlyCountryName         OBJ_pilotAttributeType,43L

#define LN_organizationalStatus         "organizationalStatus"
#define NID_organizationalStatus                491
#define OBJ_organizationalStatus                OBJ_pilotAttributeType,45L

#define LN_janetMailbox         "janetMailbox"
#define NID_janetMailbox                492
#define OBJ_janetMailbox                OBJ_pilotAttributeType,46L

#define LN_mailPreferenceOption         "mailPreferenceOption"
#define NID_mailPreferenceOption                493
#define OBJ_mailPreferenceOption                OBJ_pilotAttributeType,47L

#define LN_buildingName         "buildingName"
#define NID_buildingName                494
#define OBJ_buildingName                OBJ_pilotAttributeType,48L

#define LN_dSAQuality           "dSAQuality"
#define NID_dSAQuality          495
#define OBJ_dSAQuality          OBJ_pilotAttributeType,49L

#define LN_singleLevelQuality           "singleLevelQuality"
#define NID_singleLevelQuality          496
#define OBJ_singleLevelQuality          OBJ_pilotAttributeType,50L

#define LN_subtreeMinimumQuality                "subtreeMinimumQuality"
#define NID_subtreeMinimumQuality               497
#define OBJ_subtreeMinimumQuality               OBJ_pilotAttributeType,51L

#define LN_subtreeMaximumQuality                "subtreeMaximumQuality"
#define NID_subtreeMaximumQuality               498
#define OBJ_subtreeMaximumQuality               OBJ_pilotAttributeType,52L

#define LN_personalSignature            "personalSignature"
#define NID_personalSignature           499
#define OBJ_personalSignature           OBJ_pilotAttributeType,53L

#define LN_dITRedirect          "dITRedirect"
#define NID_dITRedirect         500
#define OBJ_dITRedirect         OBJ_pilotAttributeType,54L

#define SN_audio                "audio"
#define NID_audio               501
#define OBJ_audio               OBJ_pilotAttributeType,55L

#define LN_documentPublisher            "documentPublisher"
#define NID_documentPublisher           502
#define OBJ_documentPublisher           OBJ_pilotAttributeType,56L

#define SN_id_set               "id-set"
#define LN_id_set               "Secure Electronic Transactions"
#define NID_id_set              512
#define OBJ_id_set              OBJ_international_organizations,42L

#define SN_set_ctype            "set-ctype"
#define LN_set_ctype            "content types"
#define NID_set_ctype           513
#define OBJ_set_ctype           OBJ_id_set,0L

#define SN_set_msgExt           "set-msgExt"
#define LN_set_msgExt           "message extensions"
#define NID_set_msgExt          514
#define OBJ_set_msgExt          OBJ_id_set,1L

#define SN_set_attr             "set-attr"
#define NID_set_attr            515
#define OBJ_set_attr            OBJ_id_set,3L

#define SN_set_policy           "set-policy"
#define NID_set_policy          516
#define OBJ_set_policy          OBJ_id_set,5L

#define SN_set_certExt          "set-certExt"
#define LN_set_certExt          "certificate extensions"
#define NID_set_certExt         517
#define OBJ_set_certExt         OBJ_id_set,7L

#define SN_set_brand            "set-brand"
#define NID_set_brand           518
#define OBJ_set_brand           OBJ_id_set,8L

#define SN_setct_PANData                "setct-PANData"
#define NID_setct_PANData               519
#define OBJ_setct_PANData               OBJ_set_ctype,0L

#define SN_setct_PANToken               "setct-PANToken"
#define NID_setct_PANToken              520
#define OBJ_setct_PANToken              OBJ_set_ctype,1L

#define SN_setct_PANOnly                "setct-PANOnly"
#define NID_setct_PANOnly               521
#define OBJ_setct_PANOnly               OBJ_set_ctype,2L

#define SN_setct_OIData         "setct-OIData"
#define NID_setct_OIData                522
#define OBJ_setct_OIData                OBJ_set_ctype,3L

#define SN_setct_PI             "setct-PI"
#define NID_setct_PI            523
#define OBJ_setct_PI            OBJ_set_ctype,4L

#define SN_setct_PIData         "setct-PIData"
#define NID_setct_PIData                524
#define OBJ_setct_PIData                OBJ_set_ctype,5L

#define SN_setct_PIDataUnsigned         "setct-PIDataUnsigned"
#define NID_setct_PIDataUnsigned                525
#define OBJ_setct_PIDataUnsigned                OBJ_set_ctype,6L

#define SN_setct_HODInput               "setct-HODInput"
#define NID_setct_HODInput              526
#define OBJ_setct_HODInput              OBJ_set_ctype,7L

#define SN_setct_AuthResBaggage         "setct-AuthResBaggage"
#define NID_setct_AuthResBaggage                527
#define OBJ_setct_AuthResBaggage                OBJ_set_ctype,8L

#define SN_setct_AuthRevReqBaggage              "setct-AuthRevReqBaggage"
#define NID_setct_AuthRevReqBaggage             528
#define OBJ_setct_AuthRevReqBaggage             OBJ_set_ctype,9L

#define SN_setct_AuthRevResBaggage              "setct-AuthRevResBaggage"
#define NID_setct_AuthRevResBaggage             529
#define OBJ_setct_AuthRevResBaggage             OBJ_set_ctype,10L

#define SN_setct_CapTokenSeq            "setct-CapTokenSeq"
#define NID_setct_CapTokenSeq           530
#define OBJ_setct_CapTokenSeq           OBJ_set_ctype,11L

#define SN_setct_PInitResData           "setct-PInitResData"
#define NID_setct_PInitResData          531
#define OBJ_setct_PInitResData          OBJ_set_ctype,12L

#define SN_setct_PI_TBS         "setct-PI-TBS"
#define NID_setct_PI_TBS                532
#define OBJ_setct_PI_TBS                OBJ_set_ctype,13L

#define SN_setct_PResData               "setct-PResData"
#define NID_setct_PResData              533
#define OBJ_setct_PResData              OBJ_set_ctype,14L

#define SN_setct_AuthReqTBS             "setct-AuthReqTBS"
#define NID_setct_AuthReqTBS            534
#define OBJ_setct_AuthReqTBS            OBJ_set_ctype,16L

#define SN_setct_AuthResTBS             "setct-AuthResTBS"
#define NID_setct_AuthResTBS            535
#define OBJ_setct_AuthResTBS            OBJ_set_ctype,17L

#define SN_setct_AuthResTBSX            "setct-AuthResTBSX"
#define NID_setct_AuthResTBSX           536
#define OBJ_setct_AuthResTBSX           OBJ_set_ctype,18L

#define SN_setct_AuthTokenTBS           "setct-AuthTokenTBS"
#define NID_setct_AuthTokenTBS          537
#define OBJ_setct_AuthTokenTBS          OBJ_set_ctype,19L

#define SN_setct_CapTokenData           "setct-CapTokenData"
#define NID_setct_CapTokenData          538
#define OBJ_setct_CapTokenData          OBJ_set_ctype,20L

#define SN_setct_CapTokenTBS            "setct-CapTokenTBS"
#define NID_setct_CapTokenTBS           539
#define OBJ_setct_CapTokenTBS           OBJ_set_ctype,21L

#define SN_setct_AcqCardCodeMsg         "setct-AcqCardCodeMsg"
#define NID_setct_AcqCardCodeMsg                540
#define OBJ_setct_AcqCardCodeMsg                OBJ_set_ctype,22L

#define SN_setct_AuthRevReqTBS          "setct-AuthRevReqTBS"
#define NID_setct_AuthRevReqTBS         541
#define OBJ_setct_AuthRevReqTBS         OBJ_set_ctype,23L

#define SN_setct_AuthRevResData         "setct-AuthRevResData"
#define NID_setct_AuthRevResData                542
#define OBJ_setct_AuthRevResData                OBJ_set_ctype,24L

#define SN_setct_AuthRevResTBS          "setct-AuthRevResTBS"
#define NID_setct_AuthRevResTBS         543
#define OBJ_setct_AuthRevResTBS         OBJ_set_ctype,25L

#define SN_setct_CapReqTBS              "setct-CapReqTBS"
#define NID_setct_CapReqTBS             544
#define OBJ_setct_CapReqTBS             OBJ_set_ctype,26L

#define SN_setct_CapReqTBSX             "setct-CapReqTBSX"
#define NID_setct_CapReqTBSX            545
#define OBJ_setct_CapReqTBSX            OBJ_set_ctype,27L

#define SN_setct_CapResData             "setct-CapResData"
#define NID_setct_CapResData            546
#define OBJ_setct_CapResData            OBJ_set_ctype,28L

#define SN_setct_CapRevReqTBS           "setct-CapRevReqTBS"
#define NID_setct_CapRevReqTBS          547
#define OBJ_setct_CapRevReqTBS          OBJ_set_ctype,29L

#define SN_setct_CapRevReqTBSX          "setct-CapRevReqTBSX"
#define NID_setct_CapRevReqTBSX         548
#define OBJ_setct_CapRevReqTBSX         OBJ_set_ctype,30L

#define SN_setct_CapRevResData          "setct-CapRevResData"
#define NID_setct_CapRevResData         549
#define OBJ_setct_CapRevResData         OBJ_set_ctype,31L

#define SN_setct_CredReqTBS             "setct-CredReqTBS"
#define NID_setct_CredReqTBS            550
#define OBJ_setct_CredReqTBS            OBJ_set_ctype,32L

#define SN_setct_CredReqTBSX            "setct-CredReqTBSX"
#define NID_setct_CredReqTBSX           551
#define OBJ_setct_CredReqTBSX           OBJ_set_ctype,33L

#define SN_setct_CredResData            "setct-CredResData"
#define NID_setct_CredResData           552
#define OBJ_setct_CredResData           OBJ_set_ctype,34L

#define SN_setct_CredRevReqTBS          "setct-CredRevReqTBS"
#define NID_setct_CredRevReqTBS         553
#define OBJ_setct_CredRevReqTBS         OBJ_set_ctype,35L

#define SN_setct_CredRevReqTBSX         "setct-CredRevReqTBSX"
#define NID_setct_CredRevReqTBSX                554
#define OBJ_setct_CredRevReqTBSX                OBJ_set_ctype,36L

#define SN_setct_CredRevResData         "setct-CredRevResData"
#define NID_setct_CredRevResData                555
#define OBJ_setct_CredRevResData                OBJ_set_ctype,37L

#define SN_setct_PCertReqData           "setct-PCertReqData"
#define NID_setct_PCertReqData          556
#define OBJ_setct_PCertReqData          OBJ_set_ctype,38L

#define SN_setct_PCertResTBS            "setct-PCertResTBS"
#define NID_setct_PCertResTBS           557
#define OBJ_setct_PCertResTBS           OBJ_set_ctype,39L

#define SN_setct_BatchAdminReqData              "setct-BatchAdminReqData"
#define NID_setct_BatchAdminReqData             558
#define OBJ_setct_BatchAdminReqData             OBJ_set_ctype,40L

#define SN_setct_BatchAdminResData              "setct-BatchAdminResData"
#define NID_setct_BatchAdminResData             559
#define OBJ_setct_BatchAdminResData             OBJ_set_ctype,41L

#define SN_setct_CardCInitResTBS                "setct-CardCInitResTBS"
#define NID_setct_CardCInitResTBS               560
#define OBJ_setct_CardCInitResTBS               OBJ_set_ctype,42L

#define SN_setct_MeAqCInitResTBS                "setct-MeAqCInitResTBS"
#define NID_setct_MeAqCInitResTBS               561
#define OBJ_setct_MeAqCInitResTBS               OBJ_set_ctype,43L

#define SN_setct_RegFormResTBS          "setct-RegFormResTBS"
#define NID_setct_RegFormResTBS         562
#define OBJ_setct_RegFormResTBS         OBJ_set_ctype,44L

#define SN_setct_CertReqData            "setct-CertReqData"
#define NID_setct_CertReqData           563
#define OBJ_setct_CertReqData           OBJ_set_ctype,45L

#define SN_setct_CertReqTBS             "setct-CertReqTBS"
#define NID_setct_CertReqTBS            564
#define OBJ_setct_CertReqTBS            OBJ_set_ctype,46L

#define SN_setct_CertResData            "setct-CertResData"
#define NID_setct_CertResData           565
#define OBJ_setct_CertResData           OBJ_set_ctype,47L

#define SN_setct_CertInqReqTBS          "setct-CertInqReqTBS"
#define NID_setct_CertInqReqTBS         566
#define OBJ_setct_CertInqReqTBS         OBJ_set_ctype,48L

#define SN_setct_ErrorTBS               "setct-ErrorTBS"
#define NID_setct_ErrorTBS              567
#define OBJ_setct_ErrorTBS              OBJ_set_ctype,49L

#define SN_setct_PIDualSignedTBE                "setct-PIDualSignedTBE"
#define NID_setct_PIDualSignedTBE               568
#define OBJ_setct_PIDualSignedTBE               OBJ_set_ctype,50L

#define SN_setct_PIUnsignedTBE          "setct-PIUnsignedTBE"
#define NID_setct_PIUnsignedTBE         569
#define OBJ_setct_PIUnsignedTBE         OBJ_set_ctype,51L

#define SN_setct_AuthReqTBE             "setct-AuthReqTBE"
#define NID_setct_AuthReqTBE            570
#define OBJ_setct_AuthReqTBE            OBJ_set_ctype,52L

#define SN_setct_AuthResTBE             "setct-AuthResTBE"
#define NID_setct_AuthResTBE            571
#define OBJ_setct_AuthResTBE            OBJ_set_ctype,53L

#define SN_setct_AuthResTBEX            "setct-AuthResTBEX"
#define NID_setct_AuthResTBEX           572
#define OBJ_setct_AuthResTBEX           OBJ_set_ctype,54L

#define SN_setct_AuthTokenTBE           "setct-AuthTokenTBE"
#define NID_setct_AuthTokenTBE          573
#define OBJ_setct_AuthTokenTBE          OBJ_set_ctype,55L

#define SN_setct_CapTokenTBE            "setct-CapTokenTBE"
#define NID_setct_CapTokenTBE           574
#define OBJ_setct_CapTokenTBE           OBJ_set_ctype,56L

#define SN_setct_CapTokenTBEX           "setct-CapTokenTBEX"
#define NID_setct_CapTokenTBEX          575
#define OBJ_setct_CapTokenTBEX          OBJ_set_ctype,57L

#define SN_setct_AcqCardCodeMsgTBE              "setct-AcqCardCodeMsgTBE"
#define NID_setct_AcqCardCodeMsgTBE             576
#define OBJ_setct_AcqCardCodeMsgTBE             OBJ_set_ctype,58L

#define SN_setct_AuthRevReqTBE          "setct-AuthRevReqTBE"
#define NID_setct_AuthRevReqTBE         577
#define OBJ_setct_AuthRevReqTBE         OBJ_set_ctype,59L

#define SN_setct_AuthRevResTBE          "setct-AuthRevResTBE"
#define NID_setct_AuthRevResTBE         578
#define OBJ_setct_AuthRevResTBE         OBJ_set_ctype,60L

#define SN_setct_AuthRevResTBEB         "setct-AuthRevResTBEB"
#define NID_setct_AuthRevResTBEB                579
#define OBJ_setct_AuthRevResTBEB                OBJ_set_ctype,61L

#define SN_setct_CapReqTBE              "setct-CapReqTBE"
#define NID_setct_CapReqTBE             580
#define OBJ_setct_CapReqTBE             OBJ_set_ctype,62L

#define SN_setct_CapReqTBEX             "setct-CapReqTBEX"
#define NID_setct_CapReqTBEX            581
#define OBJ_setct_CapReqTBEX            OBJ_set_ctype,63L

#define SN_setct_CapResTBE              "setct-CapResTBE"
#define NID_setct_CapResTBE             582
#define OBJ_setct_CapResTBE             OBJ_set_ctype,64L

#define SN_setct_CapRevReqTBE           "setct-CapRevReqTBE"
#define NID_setct_CapRevReqTBE          583
#define OBJ_setct_CapRevReqTBE          OBJ_set_ctype,65L

#define SN_setct_CapRevReqTBEX          "setct-CapRevReqTBEX"
#define NID_setct_CapRevReqTBEX         584
#define OBJ_setct_CapRevReqTBEX         OBJ_set_ctype,66L

#define SN_setct_CapRevResTBE           "setct-CapRevResTBE"
#define NID_setct_CapRevResTBE          585
#define OBJ_setct_CapRevResTBE          OBJ_set_ctype,67L

#define SN_setct_CredReqTBE             "setct-CredReqTBE"
#define NID_setct_CredReqTBE            586
#define OBJ_setct_CredReqTBE            OBJ_set_ctype,68L

#define SN_setct_CredReqTBEX            "setct-CredReqTBEX"
#define NID_setct_CredReqTBEX           587
#define OBJ_setct_CredReqTBEX           OBJ_set_ctype,69L

#define SN_setct_CredResTBE             "setct-CredResTBE"
#define NID_setct_CredResTBE            588
#define OBJ_setct_CredResTBE            OBJ_set_ctype,70L

#define SN_setct_CredRevReqTBE          "setct-CredRevReqTBE"
#define NID_setct_CredRevReqTBE         589
#define OBJ_setct_CredRevReqTBE         OBJ_set_ctype,71L

#define SN_setct_CredRevReqTBEX         "setct-CredRevReqTBEX"
#define NID_setct_CredRevReqTBEX                590
#define OBJ_setct_CredRevReqTBEX                OBJ_set_ctype,72L

#define SN_setct_CredRevResTBE          "setct-CredRevResTBE"
#define NID_setct_CredRevResTBE         591
#define OBJ_setct_CredRevResTBE         OBJ_set_ctype,73L

#define SN_setct_BatchAdminReqTBE               "setct-BatchAdminReqTBE"
#define NID_setct_BatchAdminReqTBE              592
#define OBJ_setct_BatchAdminReqTBE              OBJ_set_ctype,74L

#define SN_setct_BatchAdminResTBE               "setct-BatchAdminResTBE"
#define NID_setct_BatchAdminResTBE              593
#define OBJ_setct_BatchAdminResTBE              OBJ_set_ctype,75L

#define SN_setct_RegFormReqTBE          "setct-RegFormReqTBE"
#define NID_setct_RegFormReqTBE         594
#define OBJ_setct_RegFormReqTBE         OBJ_set_ctype,76L

#define SN_setct_CertReqTBE             "setct-CertReqTBE"
#define NID_setct_CertReqTBE            595
#define OBJ_setct_CertReqTBE            OBJ_set_ctype,77L

#define SN_setct_CertReqTBEX            "setct-CertReqTBEX"
#define NID_setct_CertReqTBEX           596
#define OBJ_setct_CertReqTBEX           OBJ_set_ctype,78L

#define SN_setct_CertResTBE             "setct-CertResTBE"
#define NID_setct_CertResTBE            597
#define OBJ_setct_CertResTBE            OBJ_set_ctype,79L

#define SN_setct_CRLNotificationTBS             "setct-CRLNotificationTBS"
#define NID_setct_CRLNotificationTBS            598
#define OBJ_setct_CRLNotificationTBS            OBJ_set_ctype,80L

#define SN_setct_CRLNotificationResTBS          "setct-CRLNotificationResTBS"
#define NID_setct_CRLNotificationResTBS         599
#define OBJ_setct_CRLNotificationResTBS         OBJ_set_ctype,81L

#define SN_setct_BCIDistributionTBS             "setct-BCIDistributionTBS"
#define NID_setct_BCIDistributionTBS            600
#define OBJ_setct_BCIDistributionTBS            OBJ_set_ctype,82L

#define SN_setext_genCrypt              "setext-genCrypt"
#define LN_setext_genCrypt              "generic cryptogram"
#define NID_setext_genCrypt             601
#define OBJ_setext_genCrypt             OBJ_set_msgExt,1L

#define SN_setext_miAuth                "setext-miAuth"
#define LN_setext_miAuth                "merchant initiated auth"
#define NID_setext_miAuth               602
#define OBJ_setext_miAuth               OBJ_set_msgExt,3L

#define SN_setext_pinSecure             "setext-pinSecure"
#define NID_setext_pinSecure            603
#define OBJ_setext_pinSecure            OBJ_set_msgExt,4L

#define SN_setext_pinAny                "setext-pinAny"
#define NID_setext_pinAny               604
#define OBJ_setext_pinAny               OBJ_set_msgExt,5L

#define SN_setext_track2                "setext-track2"
#define NID_setext_track2               605
#define OBJ_setext_track2               OBJ_set_msgExt,7L

#define SN_setext_cv            "setext-cv"
#define LN_setext_cv            "additional verification"
#define NID_setext_cv           606
#define OBJ_setext_cv           OBJ_set_msgExt,8L

#define SN_set_policy_root              "set-policy-root"
#define NID_set_policy_root             607
#define OBJ_set_policy_root             OBJ_set_policy,0L

#define SN_setCext_hashedRoot           "setCext-hashedRoot"
#define NID_setCext_hashedRoot          608
#define OBJ_setCext_hashedRoot          OBJ_set_certExt,0L

#define SN_setCext_certType             "setCext-certType"
#define NID_setCext_certType            609
#define OBJ_setCext_certType            OBJ_set_certExt,1L

#define SN_setCext_merchData            "setCext-merchData"
#define NID_setCext_merchData           610
#define OBJ_setCext_merchData           OBJ_set_certExt,2L

#define SN_setCext_cCertRequired                "setCext-cCertRequired"
#define NID_setCext_cCertRequired               611
#define OBJ_setCext_cCertRequired               OBJ_set_certExt,3L

#define SN_setCext_tunneling            "setCext-tunneling"
#define NID_setCext_tunneling           612
#define OBJ_setCext_tunneling           OBJ_set_certExt,4L

#define SN_setCext_setExt               "setCext-setExt"
#define NID_setCext_setExt              613
#define OBJ_setCext_setExt              OBJ_set_certExt,5L

#define SN_setCext_setQualf             "setCext-setQualf"
#define NID_setCext_setQualf            614
#define OBJ_setCext_setQualf            OBJ_set_certExt,6L

#define SN_setCext_PGWYcapabilities             "setCext-PGWYcapabilities"
#define NID_setCext_PGWYcapabilities            615
#define OBJ_setCext_PGWYcapabilities            OBJ_set_certExt,7L

#define SN_setCext_TokenIdentifier              "setCext-TokenIdentifier"
#define NID_setCext_TokenIdentifier             616
#define OBJ_setCext_TokenIdentifier             OBJ_set_certExt,8L

#define SN_setCext_Track2Data           "setCext-Track2Data"
#define NID_setCext_Track2Data          617
#define OBJ_setCext_Track2Data          OBJ_set_certExt,9L

#define SN_setCext_TokenType            "setCext-TokenType"
#define NID_setCext_TokenType           618
#define OBJ_setCext_TokenType           OBJ_set_certExt,10L

#define SN_setCext_IssuerCapabilities           "setCext-IssuerCapabilities"
#define NID_setCext_IssuerCapabilities          619
#define OBJ_setCext_IssuerCapabilities          OBJ_set_certExt,11L

#define SN_setAttr_Cert         "setAttr-Cert"
#define NID_setAttr_Cert                620
#define OBJ_setAttr_Cert                OBJ_set_attr,0L

#define SN_setAttr_PGWYcap              "setAttr-PGWYcap"
#define LN_setAttr_PGWYcap              "payment gateway capabilities"
#define NID_setAttr_PGWYcap             621
#define OBJ_setAttr_PGWYcap             OBJ_set_attr,1L

#define SN_setAttr_TokenType            "setAttr-TokenType"
#define NID_setAttr_TokenType           622
#define OBJ_setAttr_TokenType           OBJ_set_attr,2L

#define SN_setAttr_IssCap               "setAttr-IssCap"
#define LN_setAttr_IssCap               "issuer capabilities"
#define NID_setAttr_IssCap              623
#define OBJ_setAttr_IssCap              OBJ_set_attr,3L

#define SN_set_rootKeyThumb             "set-rootKeyThumb"
#define NID_set_rootKeyThumb            624
#define OBJ_set_rootKeyThumb            OBJ_setAttr_Cert,0L

#define SN_set_addPolicy                "set-addPolicy"
#define NID_set_addPolicy               625
#define OBJ_set_addPolicy               OBJ_setAttr_Cert,1L

#define SN_setAttr_Token_EMV            "setAttr-Token-EMV"
#define NID_setAttr_Token_EMV           626
#define OBJ_setAttr_Token_EMV           OBJ_setAttr_TokenType,1L

#define SN_setAttr_Token_B0Prime                "setAttr-Token-B0Prime"
#define NID_setAttr_Token_B0Prime               627
#define OBJ_setAttr_Token_B0Prime               OBJ_setAttr_TokenType,2L

#define SN_setAttr_IssCap_CVM           "setAttr-IssCap-CVM"
#define NID_setAttr_IssCap_CVM          628
#define OBJ_setAttr_IssCap_CVM          OBJ_setAttr_IssCap,3L

#define SN_setAttr_IssCap_T2            "setAttr-IssCap-T2"
#define NID_setAttr_IssCap_T2           629
#define OBJ_setAttr_IssCap_T2           OBJ_setAttr_IssCap,4L

#define SN_setAttr_IssCap_Sig           "setAttr-IssCap-Sig"
#define NID_setAttr_IssCap_Sig          630
#define OBJ_setAttr_IssCap_Sig          OBJ_setAttr_IssCap,5L

#define SN_setAttr_GenCryptgrm          "setAttr-GenCryptgrm"
#define LN_setAttr_GenCryptgrm          "generate cryptogram"
#define NID_setAttr_GenCryptgrm         631
#define OBJ_setAttr_GenCryptgrm         OBJ_setAttr_IssCap_CVM,1L

#define SN_setAttr_T2Enc                "setAttr-T2Enc"
#define LN_setAttr_T2Enc                "encrypted track 2"
#define NID_setAttr_T2Enc               632
#define OBJ_setAttr_T2Enc               OBJ_setAttr_IssCap_T2,1L

#define SN_setAttr_T2cleartxt           "setAttr-T2cleartxt"
#define LN_setAttr_T2cleartxt           "cleartext track 2"
#define NID_setAttr_T2cleartxt          633
#define OBJ_setAttr_T2cleartxt          OBJ_setAttr_IssCap_T2,2L

#define SN_setAttr_TokICCsig            "setAttr-TokICCsig"
#define LN_setAttr_TokICCsig            "ICC or token signature"
#define NID_setAttr_TokICCsig           634
#define OBJ_setAttr_TokICCsig           OBJ_setAttr_IssCap_Sig,1L

#define SN_setAttr_SecDevSig            "setAttr-SecDevSig"
#define LN_setAttr_SecDevSig            "secure device signature"
#define NID_setAttr_SecDevSig           635
#define OBJ_setAttr_SecDevSig           OBJ_setAttr_IssCap_Sig,2L

#define SN_set_brand_IATA_ATA           "set-brand-IATA-ATA"
#define NID_set_brand_IATA_ATA          636
#define OBJ_set_brand_IATA_ATA          OBJ_set_brand,1L

#define SN_set_brand_Diners             "set-brand-Diners"
#define NID_set_brand_Diners            637
#define OBJ_set_brand_Diners            OBJ_set_brand,30L

#define SN_set_brand_AmericanExpress            "set-brand-AmericanExpress"
#define NID_set_brand_AmericanExpress           638
#define OBJ_set_brand_AmericanExpress           OBJ_set_brand,34L

#define SN_set_brand_JCB                "set-brand-JCB"
#define NID_set_brand_JCB               639
#define OBJ_set_brand_JCB               OBJ_set_brand,35L

#define SN_set_brand_Visa               "set-brand-Visa"
#define NID_set_brand_Visa              640
#define OBJ_set_brand_Visa              OBJ_set_brand,4L

#define SN_set_brand_MasterCard         "set-brand-MasterCard"
#define NID_set_brand_MasterCard                641
#define OBJ_set_brand_MasterCard                OBJ_set_brand,5L

#define SN_set_brand_Novus              "set-brand-Novus"
#define NID_set_brand_Novus             642
#define OBJ_set_brand_Novus             OBJ_set_brand,6011L

#define SN_des_cdmf             "DES-CDMF"
#define LN_des_cdmf             "des-cdmf"
#define NID_des_cdmf            643
#define OBJ_des_cdmf            OBJ_rsadsi,3L,10L

#define SN_rsaOAEPEncryptionSET         "rsaOAEPEncryptionSET"
#define NID_rsaOAEPEncryptionSET                644
#define OBJ_rsaOAEPEncryptionSET                OBJ_rsadsi,1L,1L,6L

#define SN_ipsec3               "Oakley-EC2N-3"
#define LN_ipsec3               "ipsec3"
#define NID_ipsec3              749

#define SN_ipsec4               "Oakley-EC2N-4"
#define LN_ipsec4               "ipsec4"
#define NID_ipsec4              750

#define SN_whirlpool            "whirlpool"
#define NID_whirlpool           804
#define OBJ_whirlpool           OBJ_iso,0L,10118L,3L,0L,55L

#define SN_cryptopro            "cryptopro"
#define NID_cryptopro           805
#define OBJ_cryptopro           OBJ_member_body,643L,2L,2L

#define SN_cryptocom            "cryptocom"
#define NID_cryptocom           806
#define OBJ_cryptocom           OBJ_member_body,643L,2L,9L

#define SN_id_GostR3411_94_with_GostR3410_2001          "id-GostR3411-94-with-GostR3410-2001"
#define LN_id_GostR3411_94_with_GostR3410_2001          "GOST R 34.11-94 with GOST R 34.10-2001"
#define NID_id_GostR3411_94_with_GostR3410_2001         807
#define OBJ_id_GostR3411_94_with_GostR3410_2001         OBJ_cryptopro,3L

#define SN_id_GostR3411_94_with_GostR3410_94            "id-GostR3411-94-with-GostR3410-94"
#define LN_id_GostR3411_94_with_GostR3410_94            "GOST R 34.11-94 with GOST R 34.10-94"
#define NID_id_GostR3411_94_with_GostR3410_94           808
#define OBJ_id_GostR3411_94_with_GostR3410_94           OBJ_cryptopro,4L

#define SN_id_GostR3411_94              "md_gost94"
#define LN_id_GostR3411_94              "GOST R 34.11-94"
#define NID_id_GostR3411_94             809
#define OBJ_id_GostR3411_94             OBJ_cryptopro,9L

#define SN_id_HMACGostR3411_94          "id-HMACGostR3411-94"
#define LN_id_HMACGostR3411_94          "HMAC GOST 34.11-94"
#define NID_id_HMACGostR3411_94         810
#define OBJ_id_HMACGostR3411_94         OBJ_cryptopro,10L

#define SN_id_GostR3410_2001            "gost2001"
#define LN_id_GostR3410_2001            "GOST R 34.10-2001"
#define NID_id_GostR3410_2001           811
#define OBJ_id_GostR3410_2001           OBJ_cryptopro,19L

#define SN_id_GostR3410_94              "gost94"
#define LN_id_GostR3410_94              "GOST R 34.10-94"
#define NID_id_GostR3410_94             812
#define OBJ_id_GostR3410_94             OBJ_cryptopro,20L

#define SN_id_Gost28147_89              "gost89"
#define LN_id_Gost28147_89              "GOST 28147-89"
#define NID_id_Gost28147_89             813
#define OBJ_id_Gost28147_89             OBJ_cryptopro,21L

#define SN_gost89_cnt           "gost89-cnt"
#define NID_gost89_cnt          814

#define SN_id_Gost28147_89_MAC          "gost-mac"
#define LN_id_Gost28147_89_MAC          "GOST 28147-89 MAC"
#define NID_id_Gost28147_89_MAC         815
#define OBJ_id_Gost28147_89_MAC         OBJ_cryptopro,22L

#define SN_id_GostR3411_94_prf          "prf-gostr3411-94"
#define LN_id_GostR3411_94_prf          "GOST R 34.11-94 PRF"
#define NID_id_GostR3411_94_prf         816
#define OBJ_id_GostR3411_94_prf         OBJ_cryptopro,23L

#define SN_id_GostR3410_2001DH          "id-GostR3410-2001DH"
#define LN_id_GostR3410_2001DH          "GOST R 34.10-2001 DH"
#define NID_id_GostR3410_2001DH         817
#define OBJ_id_GostR3410_2001DH         OBJ_cryptopro,98L

#define SN_id_GostR3410_94DH            "id-GostR3410-94DH"
#define LN_id_GostR3410_94DH            "GOST R 34.10-94 DH"
#define NID_id_GostR3410_94DH           818
#define OBJ_id_GostR3410_94DH           OBJ_cryptopro,99L

#define SN_id_Gost28147_89_CryptoPro_KeyMeshing         "id-Gost28147-89-CryptoPro-KeyMeshing"
#define NID_id_Gost28147_89_CryptoPro_KeyMeshing                819
#define OBJ_id_Gost28147_89_CryptoPro_KeyMeshing                OBJ_cryptopro,14L,1L

#define SN_id_Gost28147_89_None_KeyMeshing              "id-Gost28147-89-None-KeyMeshing"
#define NID_id_Gost28147_89_None_KeyMeshing             820
#define OBJ_id_Gost28147_89_None_KeyMeshing             OBJ_cryptopro,14L,0L

#define SN_id_GostR3411_94_TestParamSet         "id-GostR3411-94-TestParamSet"
#define NID_id_GostR3411_94_TestParamSet                821
#define OBJ_id_GostR3411_94_TestParamSet                OBJ_cryptopro,30L,0L

#define SN_id_GostR3411_94_CryptoProParamSet            "id-GostR3411-94-CryptoProParamSet"
#define NID_id_GostR3411_94_CryptoProParamSet           822
#define OBJ_id_GostR3411_94_CryptoProParamSet           OBJ_cryptopro,30L,1L

#define SN_id_Gost28147_89_TestParamSet         "id-Gost28147-89-TestParamSet"
#define NID_id_Gost28147_89_TestParamSet                823
#define OBJ_id_Gost28147_89_TestParamSet                OBJ_cryptopro,31L,0L

#define SN_id_Gost28147_89_CryptoPro_A_ParamSet         "id-Gost28147-89-CryptoPro-A-ParamSet"
#define NID_id_Gost28147_89_CryptoPro_A_ParamSet                824
#define OBJ_id_Gost28147_89_CryptoPro_A_ParamSet                OBJ_cryptopro,31L,1L

#define SN_id_Gost28147_89_CryptoPro_B_ParamSet         "id-Gost28147-89-CryptoPro-B-ParamSet"
#define NID_id_Gost28147_89_CryptoPro_B_ParamSet                825
#define OBJ_id_Gost28147_89_CryptoPro_B_ParamSet                OBJ_cryptopro,31L,2L

#define SN_id_Gost28147_89_CryptoPro_C_ParamSet         "id-Gost28147-89-CryptoPro-C-ParamSet"
#define NID_id_Gost28147_89_CryptoPro_C_ParamSet                826
#define OBJ_id_Gost28147_89_CryptoPro_C_ParamSet                OBJ_cryptopro,31L,3L

#define SN_id_Gost28147_89_CryptoPro_D_ParamSet         "id-Gost28147-89-CryptoPro-D-ParamSet"
#define NID_id_Gost28147_89_CryptoPro_D_ParamSet                827
#define OBJ_id_Gost28147_89_CryptoPro_D_ParamSet                OBJ_cryptopro,31L,4L

#define SN_id_Gost28147_89_CryptoPro_Oscar_1_1_ParamSet         "id-Gost28147-89-CryptoPro-Oscar-1-1-ParamSet"
#define NID_id_Gost28147_89_CryptoPro_Oscar_1_1_ParamSet                828
#define OBJ_id_Gost28147_89_CryptoPro_Oscar_1_1_ParamSet                OBJ_cryptopro,31L,5L

#define SN_id_Gost28147_89_CryptoPro_Oscar_1_0_ParamSet         "id-Gost28147-89-CryptoPro-Oscar-1-0-ParamSet"
#define NID_id_Gost28147_89_CryptoPro_Oscar_1_0_ParamSet                829
#define OBJ_id_Gost28147_89_CryptoPro_Oscar_1_0_ParamSet                OBJ_cryptopro,31L,6L

#define SN_id_Gost28147_89_CryptoPro_RIC_1_ParamSet             "id-Gost28147-89-CryptoPro-RIC-1-ParamSet"
#define NID_id_Gost28147_89_CryptoPro_RIC_1_ParamSet            830
#define OBJ_id_Gost28147_89_CryptoPro_RIC_1_ParamSet            OBJ_cryptopro,31L,7L

#define SN_id_GostR3410_94_TestParamSet         "id-GostR3410-94-TestParamSet"
#define NID_id_GostR3410_94_TestParamSet                831
#define OBJ_id_GostR3410_94_TestParamSet                OBJ_cryptopro,32L,0L

#define SN_id_GostR3410_94_CryptoPro_A_ParamSet         "id-GostR3410-94-CryptoPro-A-ParamSet"
#define NID_id_GostR3410_94_CryptoPro_A_ParamSet                832
#define OBJ_id_GostR3410_94_CryptoPro_A_ParamSet                OBJ_cryptopro,32L,2L

#define SN_id_GostR3410_94_CryptoPro_B_ParamSet         "id-GostR3410-94-CryptoPro-B-ParamSet"
#define NID_id_GostR3410_94_CryptoPro_B_ParamSet                833
#define OBJ_id_GostR3410_94_CryptoPro_B_ParamSet                OBJ_cryptopro,32L,3L

#define SN_id_GostR3410_94_CryptoPro_C_ParamSet         "id-GostR3410-94-CryptoPro-C-ParamSet"
#define NID_id_GostR3410_94_CryptoPro_C_ParamSet                834
#define OBJ_id_GostR3410_94_CryptoPro_C_ParamSet                OBJ_cryptopro,32L,4L

#define SN_id_GostR3410_94_CryptoPro_D_ParamSet         "id-GostR3410-94-CryptoPro-D-ParamSet"
#define NID_id_GostR3410_94_CryptoPro_D_ParamSet                835
#define OBJ_id_GostR3410_94_CryptoPro_D_ParamSet                OBJ_cryptopro,32L,5L

#define SN_id_GostR3410_94_CryptoPro_XchA_ParamSet              "id-GostR3410-94-CryptoPro-XchA-ParamSet"
#define NID_id_GostR3410_94_CryptoPro_XchA_ParamSet             836
#define OBJ_id_GostR3410_94_CryptoPro_XchA_ParamSet             OBJ_cryptopro,33L,1L

#define SN_id_GostR3410_94_CryptoPro_XchB_ParamSet              "id-GostR3410-94-CryptoPro-XchB-ParamSet"
#define NID_id_GostR3410_94_CryptoPro_XchB_ParamSet             837
#define OBJ_id_GostR3410_94_CryptoPro_XchB_ParamSet             OBJ_cryptopro,33L,2L

#define SN_id_GostR3410_94_CryptoPro_XchC_ParamSet              "id-GostR3410-94-CryptoPro-XchC-ParamSet"
#define NID_id_GostR3410_94_CryptoPro_XchC_ParamSet             838
#define OBJ_id_GostR3410_94_CryptoPro_XchC_ParamSet             OBJ_cryptopro,33L,3L

#define SN_id_GostR3410_2001_TestParamSet               "id-GostR3410-2001-TestParamSet"
#define NID_id_GostR3410_2001_TestParamSet              839
#define OBJ_id_GostR3410_2001_TestParamSet              OBJ_cryptopro,35L,0L

#define SN_id_GostR3410_2001_CryptoPro_A_ParamSet               "id-GostR3410-2001-CryptoPro-A-ParamSet"
#define NID_id_GostR3410_2001_CryptoPro_A_ParamSet              840
#define OBJ_id_GostR3410_2001_CryptoPro_A_ParamSet              OBJ_cryptopro,35L,1L

#define SN_id_GostR3410_2001_CryptoPro_B_ParamSet               "id-GostR3410-2001-CryptoPro-B-ParamSet"
#define NID_id_GostR3410_2001_CryptoPro_B_ParamSet              841
#define OBJ_id_GostR3410_2001_CryptoPro_B_ParamSet              OBJ_cryptopro,35L,2L

#define SN_id_GostR3410_2001_CryptoPro_C_ParamSet               "id-GostR3410-2001-CryptoPro-C-ParamSet"
#define NID_id_GostR3410_2001_CryptoPro_C_ParamSet              842
#define OBJ_id_GostR3410_2001_CryptoPro_C_ParamSet              OBJ_cryptopro,35L,3L

#define SN_id_GostR3410_2001_CryptoPro_XchA_ParamSet            "id-GostR3410-2001-CryptoPro-XchA-ParamSet"
#define NID_id_GostR3410_2001_CryptoPro_XchA_ParamSet           843
#define OBJ_id_GostR3410_2001_CryptoPro_XchA_ParamSet           OBJ_cryptopro,36L,0L

#define SN_id_GostR3410_2001_CryptoPro_XchB_ParamSet            "id-GostR3410-2001-CryptoPro-XchB-ParamSet"
#define NID_id_GostR3410_2001_CryptoPro_XchB_ParamSet           844
#define OBJ_id_GostR3410_2001_CryptoPro_XchB_ParamSet           OBJ_cryptopro,36L,1L

#define SN_id_GostR3410_94_a            "id-GostR3410-94-a"
#define NID_id_GostR3410_94_a           845
#define OBJ_id_GostR3410_94_a           OBJ_id_GostR3410_94,1L

#define SN_id_GostR3410_94_aBis         "id-GostR3410-94-aBis"
#define NID_id_GostR3410_94_aBis                846
#define OBJ_id_GostR3410_94_aBis                OBJ_id_GostR3410_94,2L

#define SN_id_GostR3410_94_b            "id-GostR3410-94-b"
#define NID_id_GostR3410_94_b           847
#define OBJ_id_GostR3410_94_b           OBJ_id_GostR3410_94,3L

#define SN_id_GostR3410_94_bBis         "id-GostR3410-94-bBis"
#define NID_id_GostR3410_94_bBis                848
#define OBJ_id_GostR3410_94_bBis                OBJ_id_GostR3410_94,4L

#define SN_id_Gost28147_89_cc           "id-Gost28147-89-cc"
#define LN_id_Gost28147_89_cc           "GOST 28147-89 Cryptocom ParamSet"
#define NID_id_Gost28147_89_cc          849
#define OBJ_id_Gost28147_89_cc          OBJ_cryptocom,1L,6L,1L

#define SN_id_GostR3410_94_cc           "gost94cc"
#define LN_id_GostR3410_94_cc           "GOST 34.10-94 Cryptocom"
#define NID_id_GostR3410_94_cc          850
#define OBJ_id_GostR3410_94_cc          OBJ_cryptocom,1L,5L,3L

#define SN_id_GostR3410_2001_cc         "gost2001cc"
#define LN_id_GostR3410_2001_cc         "GOST 34.10-2001 Cryptocom"
#define NID_id_GostR3410_2001_cc                851
#define OBJ_id_GostR3410_2001_cc                OBJ_cryptocom,1L,5L,4L

#define SN_id_GostR3411_94_with_GostR3410_94_cc         "id-GostR3411-94-with-GostR3410-94-cc"
#define LN_id_GostR3411_94_with_GostR3410_94_cc         "GOST R 34.11-94 with GOST R 34.10-94 Cryptocom"
#define NID_id_GostR3411_94_with_GostR3410_94_cc                852
#define OBJ_id_GostR3411_94_with_GostR3410_94_cc                OBJ_cryptocom,1L,3L,3L

#define SN_id_GostR3411_94_with_GostR3410_2001_cc               "id-GostR3411-94-with-GostR3410-2001-cc"
#define LN_id_GostR3411_94_with_GostR3410_2001_cc               "GOST R 34.11-94 with GOST R 34.10-2001 Cryptocom"
#define NID_id_GostR3411_94_with_GostR3410_2001_cc              853
#define OBJ_id_GostR3411_94_with_GostR3410_2001_cc              OBJ_cryptocom,1L,3L,4L

#define SN_id_GostR3410_2001_ParamSet_cc                "id-GostR3410-2001-ParamSet-cc"
#define LN_id_GostR3410_2001_ParamSet_cc                "GOST R 3410-2001 Parameter Set Cryptocom"
#define NID_id_GostR3410_2001_ParamSet_cc               854
#define OBJ_id_GostR3410_2001_ParamSet_cc               OBJ_cryptocom,1L,8L,1L

#define SN_camellia_128_cbc             "CAMELLIA-128-CBC"
#define LN_camellia_128_cbc             "camellia-128-cbc"
#define NID_camellia_128_cbc            751
#define OBJ_camellia_128_cbc            1L,2L,392L,200011L,61L,1L,1L,1L,2L

#define SN_camellia_192_cbc             "CAMELLIA-192-CBC"
#define LN_camellia_192_cbc             "camellia-192-cbc"
#define NID_camellia_192_cbc            752
#define OBJ_camellia_192_cbc            1L,2L,392L,200011L,61L,1L,1L,1L,3L

#define SN_camellia_256_cbc             "CAMELLIA-256-CBC"
#define LN_camellia_256_cbc             "camellia-256-cbc"
#define NID_camellia_256_cbc            753
#define OBJ_camellia_256_cbc            1L,2L,392L,200011L,61L,1L,1L,1L,4L

#define SN_id_camellia128_wrap          "id-camellia128-wrap"
#define NID_id_camellia128_wrap         907
#define OBJ_id_camellia128_wrap         1L,2L,392L,200011L,61L,1L,1L,3L,2L

#define SN_id_camellia192_wrap          "id-camellia192-wrap"
#define NID_id_camellia192_wrap         908
#define OBJ_id_camellia192_wrap         1L,2L,392L,200011L,61L,1L,1L,3L,3L

#define SN_id_camellia256_wrap          "id-camellia256-wrap"
#define NID_id_camellia256_wrap         909
#define OBJ_id_camellia256_wrap         1L,2L,392L,200011L,61L,1L,1L,3L,4L

#define OBJ_ntt_ds              0L,3L,4401L,5L

#define OBJ_camellia            OBJ_ntt_ds,3L,1L,9L

#define SN_camellia_128_ecb             "CAMELLIA-128-ECB"
#define LN_camellia_128_ecb             "camellia-128-ecb"
#define NID_camellia_128_ecb            754
#define OBJ_camellia_128_ecb            OBJ_camellia,1L

#define SN_camellia_128_ofb128          "CAMELLIA-128-OFB"
#define LN_camellia_128_ofb128          "camellia-128-ofb"
#define NID_camellia_128_ofb128         766
#define OBJ_camellia_128_ofb128         OBJ_camellia,3L

#define SN_camellia_128_cfb128          "CAMELLIA-128-CFB"
#define LN_camellia_128_cfb128          "camellia-128-cfb"
#define NID_camellia_128_cfb128         757
#define OBJ_camellia_128_cfb128         OBJ_camellia,4L

#define SN_camellia_192_ecb             "CAMELLIA-192-ECB"
#define LN_camellia_192_ecb             "camellia-192-ecb"
#define NID_camellia_192_ecb            755
#define OBJ_camellia_192_ecb            OBJ_camellia,21L

#define SN_camellia_192_ofb128          "CAMELLIA-192-OFB"
#define LN_camellia_192_ofb128          "camellia-192-ofb"
#define NID_camellia_192_ofb128         767
#define OBJ_camellia_192_ofb128         OBJ_camellia,23L

#define SN_camellia_192_cfb128          "CAMELLIA-192-CFB"
#define LN_camellia_192_cfb128          "camellia-192-cfb"
#define NID_camellia_192_cfb128         758
#define OBJ_camellia_192_cfb128         OBJ_camellia,24L

#define SN_camellia_256_ecb             "CAMELLIA-256-ECB"
#define LN_camellia_256_ecb             "camellia-256-ecb"
#define NID_camellia_256_ecb            756
#define OBJ_camellia_256_ecb            OBJ_camellia,41L

#define SN_camellia_256_ofb128          "CAMELLIA-256-OFB"
#define LN_camellia_256_ofb128          "camellia-256-ofb"
#define NID_camellia_256_ofb128         768
#define OBJ_camellia_256_ofb128         OBJ_camellia,43L

#define SN_camellia_256_cfb128          "CAMELLIA-256-CFB"
#define LN_camellia_256_cfb128          "camellia-256-cfb"
#define NID_camellia_256_cfb128         759
#define OBJ_camellia_256_cfb128         OBJ_camellia,44L

#define SN_camellia_128_cfb1            "CAMELLIA-128-CFB1"
#define LN_camellia_128_cfb1            "camellia-128-cfb1"
#define NID_camellia_128_cfb1           760

#define SN_camellia_192_cfb1            "CAMELLIA-192-CFB1"
#define LN_camellia_192_cfb1            "camellia-192-cfb1"
#define NID_camellia_192_cfb1           761

#define SN_camellia_256_cfb1            "CAMELLIA-256-CFB1"
#define LN_camellia_256_cfb1            "camellia-256-cfb1"
#define NID_camellia_256_cfb1           762

#define SN_camellia_128_cfb8            "CAMELLIA-128-CFB8"
#define LN_camellia_128_cfb8            "camellia-128-cfb8"
#define NID_camellia_128_cfb8           763

#define SN_camellia_192_cfb8            "CAMELLIA-192-CFB8"
#define LN_camellia_192_cfb8            "camellia-192-cfb8"
#define NID_camellia_192_cfb8           764

#define SN_camellia_256_cfb8            "CAMELLIA-256-CFB8"
#define LN_camellia_256_cfb8            "camellia-256-cfb8"
#define NID_camellia_256_cfb8           765

#define SN_kisa         "KISA"
#define LN_kisa         "kisa"
#define NID_kisa                773
#define OBJ_kisa                OBJ_member_body,410L,200004L

#define SN_seed_ecb             "SEED-ECB"
#define LN_seed_ecb             "seed-ecb"
#define NID_seed_ecb            776
#define OBJ_seed_ecb            OBJ_kisa,1L,3L

#define SN_seed_cbc             "SEED-CBC"
#define LN_seed_cbc             "seed-cbc"
#define NID_seed_cbc            777
#define OBJ_seed_cbc            OBJ_kisa,1L,4L

#define SN_seed_cfb128          "SEED-CFB"
#define LN_seed_cfb128          "seed-cfb"
#define NID_seed_cfb128         779
#define OBJ_seed_cfb128         OBJ_kisa,1L,5L

#define SN_seed_ofb128          "SEED-OFB"
#define LN_seed_ofb128          "seed-ofb"
#define NID_seed_ofb128         778
#define OBJ_seed_ofb128         OBJ_kisa,1L,6L

#define SN_hmac         "HMAC"
#define LN_hmac         "hmac"
#define NID_hmac                855

#define SN_cmac         "CMAC"
#define LN_cmac         "cmac"
#define NID_cmac                894

#define SN_rc4_hmac_md5         "RC4-HMAC-MD5"
#define LN_rc4_hmac_md5         "rc4-hmac-md5"
#define NID_rc4_hmac_md5                915

#define SN_aes_128_cbc_hmac_sha1                "AES-128-CBC-HMAC-SHA1"
#define LN_aes_128_cbc_hmac_sha1                "aes-128-cbc-hmac-sha1"
#define NID_aes_128_cbc_hmac_sha1               916

#define SN_aes_192_cbc_hmac_sha1                "AES-192-CBC-HMAC-SHA1"
#define LN_aes_192_cbc_hmac_sha1                "aes-192-cbc-hmac-sha1"
#define NID_aes_192_cbc_hmac_sha1               917

#define SN_aes_256_cbc_hmac_sha1                "AES-256-CBC-HMAC-SHA1"
#define LN_aes_256_cbc_hmac_sha1                "aes-256-cbc-hmac-sha1"
#define NID_aes_256_cbc_hmac_sha1               918

#define SN_aes_128_cbc_hmac_sha256              "AES-128-CBC-HMAC-SHA256"
#define LN_aes_128_cbc_hmac_sha256              "aes-128-cbc-hmac-sha256"
#define NID_aes_128_cbc_hmac_sha256             948

#define SN_aes_192_cbc_hmac_sha256              "AES-192-CBC-HMAC-SHA256"
#define LN_aes_192_cbc_hmac_sha256              "aes-192-cbc-hmac-sha256"
#define NID_aes_192_cbc_hmac_sha256             949

#define SN_aes_256_cbc_hmac_sha256              "AES-256-CBC-HMAC-SHA256"
#define LN_aes_256_cbc_hmac_sha256              "aes-256-cbc-hmac-sha256"
#define NID_aes_256_cbc_hmac_sha256             950

#define SN_dhpublicnumber               "dhpublicnumber"
#define LN_dhpublicnumber               "X9.42 DH"
#define NID_dhpublicnumber              920
#define OBJ_dhpublicnumber              OBJ_ISO_US,10046L,2L,1L

#define SN_brainpoolP160r1              "brainpoolP160r1"
#define NID_brainpoolP160r1             921
#define OBJ_brainpoolP160r1             1L,3L,36L,3L,3L,2L,8L,1L,1L,1L

#define SN_brainpoolP160t1              "brainpoolP160t1"
#define NID_brainpoolP160t1             922
#define OBJ_brainpoolP160t1             1L,3L,36L,3L,3L,2L,8L,1L,1L,2L

#define SN_brainpoolP192r1              "brainpoolP192r1"
#define NID_brainpoolP192r1             923
#define OBJ_brainpoolP192r1             1L,3L,36L,3L,3L,2L,8L,1L,1L,3L

#define SN_brainpoolP192t1              "brainpoolP192t1"
#define NID_brainpoolP192t1             924
#define OBJ_brainpoolP192t1             1L,3L,36L,3L,3L,2L,8L,1L,1L,4L

#define SN_brainpoolP224r1              "brainpoolP224r1"
#define NID_brainpoolP224r1             925
#define OBJ_brainpoolP224r1             1L,3L,36L,3L,3L,2L,8L,1L,1L,5L

#define SN_brainpoolP224t1              "brainpoolP224t1"
#define NID_brainpoolP224t1             926
#define OBJ_brainpoolP224t1             1L,3L,36L,3L,3L,2L,8L,1L,1L,6L

#define SN_brainpoolP256r1              "brainpoolP256r1"
#define NID_brainpoolP256r1             927
#define OBJ_brainpoolP256r1             1L,3L,36L,3L,3L,2L,8L,1L,1L,7L

#define SN_brainpoolP256t1              "brainpoolP256t1"
#define NID_brainpoolP256t1             928
#define OBJ_brainpoolP256t1             1L,3L,36L,3L,3L,2L,8L,1L,1L,8L

#define SN_brainpoolP320r1              "brainpoolP320r1"
#define NID_brainpoolP320r1             929
#define OBJ_brainpoolP320r1             1L,3L,36L,3L,3L,2L,8L,1L,1L,9L

#define SN_brainpoolP320t1              "brainpoolP320t1"
#define NID_brainpoolP320t1             930
#define OBJ_brainpoolP320t1             1L,3L,36L,3L,3L,2L,8L,1L,1L,10L

#define SN_brainpoolP384r1              "brainpoolP384r1"
#define NID_brainpoolP384r1             931
#define OBJ_brainpoolP384r1             1L,3L,36L,3L,3L,2L,8L,1L,1L,11L

#define SN_brainpoolP384t1              "brainpoolP384t1"
#define NID_brainpoolP384t1             932
#define OBJ_brainpoolP384t1             1L,3L,36L,3L,3L,2L,8L,1L,1L,12L

#define SN_brainpoolP512r1              "brainpoolP512r1"
#define NID_brainpoolP512r1             933
#define OBJ_brainpoolP512r1             1L,3L,36L,3L,3L,2L,8L,1L,1L,13L

#define SN_brainpoolP512t1              "brainpoolP512t1"
#define NID_brainpoolP512t1             934
#define OBJ_brainpoolP512t1             1L,3L,36L,3L,3L,2L,8L,1L,1L,14L

#define OBJ_x9_63_scheme                1L,3L,133L,16L,840L,63L,0L

#define OBJ_secg_scheme         OBJ_certicom_arc,1L

#define SN_dhSinglePass_stdDH_sha1kdf_scheme            "dhSinglePass-stdDH-sha1kdf-scheme"
#define NID_dhSinglePass_stdDH_sha1kdf_scheme           936
#define OBJ_dhSinglePass_stdDH_sha1kdf_scheme           OBJ_x9_63_scheme,2L

#define SN_dhSinglePass_stdDH_sha224kdf_scheme          "dhSinglePass-stdDH-sha224kdf-scheme"
#define NID_dhSinglePass_stdDH_sha224kdf_scheme         937
#define OBJ_dhSinglePass_stdDH_sha224kdf_scheme         OBJ_secg_scheme,11L,0L

#define SN_dhSinglePass_stdDH_sha256kdf_scheme          "dhSinglePass-stdDH-sha256kdf-scheme"
#define NID_dhSinglePass_stdDH_sha256kdf_scheme         938
#define OBJ_dhSinglePass_stdDH_sha256kdf_scheme         OBJ_secg_scheme,11L,1L

#define SN_dhSinglePass_stdDH_sha384kdf_scheme          "dhSinglePass-stdDH-sha384kdf-scheme"
#define NID_dhSinglePass_stdDH_sha384kdf_scheme         939
#define OBJ_dhSinglePass_stdDH_sha384kdf_scheme         OBJ_secg_scheme,11L,2L

#define SN_dhSinglePass_stdDH_sha512kdf_scheme          "dhSinglePass-stdDH-sha512kdf-scheme"
#define NID_dhSinglePass_stdDH_sha512kdf_scheme         940
#define OBJ_dhSinglePass_stdDH_sha512kdf_scheme         OBJ_secg_scheme,11L,3L

#define SN_dhSinglePass_cofactorDH_sha1kdf_scheme               "dhSinglePass-cofactorDH-sha1kdf-scheme"
#define NID_dhSinglePass_cofactorDH_sha1kdf_scheme              941
#define OBJ_dhSinglePass_cofactorDH_sha1kdf_scheme              OBJ_x9_63_scheme,3L

#define SN_dhSinglePass_cofactorDH_sha224kdf_scheme             "dhSinglePass-cofactorDH-sha224kdf-scheme"
#define NID_dhSinglePass_cofactorDH_sha224kdf_scheme            942
#define OBJ_dhSinglePass_cofactorDH_sha224kdf_scheme            OBJ_secg_scheme,14L,0L

#define SN_dhSinglePass_cofactorDH_sha256kdf_scheme             "dhSinglePass-cofactorDH-sha256kdf-scheme"
#define NID_dhSinglePass_cofactorDH_sha256kdf_scheme            943
#define OBJ_dhSinglePass_cofactorDH_sha256kdf_scheme            OBJ_secg_scheme,14L,1L

#define SN_dhSinglePass_cofactorDH_sha384kdf_scheme             "dhSinglePass-cofactorDH-sha384kdf-scheme"
#define NID_dhSinglePass_cofactorDH_sha384kdf_scheme            944
#define OBJ_dhSinglePass_cofactorDH_sha384kdf_scheme            OBJ_secg_scheme,14L,2L

#define SN_dhSinglePass_cofactorDH_sha512kdf_scheme             "dhSinglePass-cofactorDH-sha512kdf-scheme"
#define NID_dhSinglePass_cofactorDH_sha512kdf_scheme            945
#define OBJ_dhSinglePass_cofactorDH_sha512kdf_scheme            OBJ_secg_scheme,14L,3L

#define SN_dh_std_kdf           "dh-std-kdf"
#define NID_dh_std_kdf          946

#define SN_dh_cofactor_kdf              "dh-cofactor-kdf"
#define NID_dh_cofactor_kdf             947

#define SN_ct_precert_scts              "ct_precert_scts"
#define LN_ct_precert_scts              "CT Precertificate SCTs"
#define NID_ct_precert_scts             951
#define OBJ_ct_precert_scts             1L,3L,6L,1L,4L,1L,11129L,2L,4L,2L

#define SN_ct_precert_poison            "ct_precert_poison"
#define LN_ct_precert_poison            "CT Precertificate Poison"
#define NID_ct_precert_poison           952
#define OBJ_ct_precert_poison           1L,3L,6L,1L,4L,1L,11129L,2L,4L,3L

#define SN_ct_precert_signer            "ct_precert_signer"
#define LN_ct_precert_signer            "CT Precertificate Signer"
#define NID_ct_precert_signer           953
#define OBJ_ct_precert_signer           1L,3L,6L,1L,4L,1L,11129L,2L,4L,4L

#define SN_ct_cert_scts         "ct_cert_scts"
#define LN_ct_cert_scts         "CT Certificate SCTs"
#define NID_ct_cert_scts                954
#define OBJ_ct_cert_scts                1L,3L,6L,1L,4L,1L,11129L,2L,4L,5L

#define SN_jurisdictionLocalityName             "jurisdictionL"
#define LN_jurisdictionLocalityName             "jurisdictionLocalityName"
#define NID_jurisdictionLocalityName            955
#define OBJ_jurisdictionLocalityName            1L,3L,6L,1L,4L,1L,311L,60L,2L,1L,1L

#define SN_jurisdictionStateOrProvinceName              "jurisdictionST"
#define LN_jurisdictionStateOrProvinceName              "jurisdictionStateOrProvinceName"
#define NID_jurisdictionStateOrProvinceName             956
#define OBJ_jurisdictionStateOrProvinceName             1L,3L,6L,1L,4L,1L,311L,60L,2L,1L,2L

#define SN_jurisdictionCountryName              "jurisdictionC"
#define LN_jurisdictionCountryName              "jurisdictionCountryName"
#define NID_jurisdictionCountryName             957
#define OBJ_jurisdictionCountryName             1L,3L,6L,1L,4L,1L,311L,60L,2L,1L,3L
