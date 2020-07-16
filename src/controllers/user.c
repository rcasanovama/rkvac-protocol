/**
 *
 *  Copyright (C) 2020  Raul Casanova Marques
 *
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "user.h"

/**
 * Gets the user identifier using the specified reader.
 *
 * @param reader the reader to be used
 * @param identifier the user identifier
 * @return 0 if success else -1
 */
int ue_get_user_identifier(reader_t reader, user_identifier_t *identifier)
{
    if (identifier == NULL)
    {
        return -1;
    }

    memcpy(identifier->buffer, (uint8_t[]) {
            0x02, 0x0F, 0x84, 0x31, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0x1A, 0x2B, 0x3C, 0x4D,
            0x5E, 0x6F, 0x00, 0x40, 0x00, 0xFF, 0x01
    }, USER_MAX_ID_LENGTH);
    identifier->buffer_length = USER_MAX_ID_LENGTH;

    return 0;
}

/**
 * Sets the revocation authority parameters and the revocation attributes.
 *
 * @param reader the reader to be used
 * @param ra_parameters the revocation authority parameters
 * @param ra_signature the signature of the user identifier
 * @return 0 if success else -1
 */
int ue_set_revocation_authority_data(reader_t reader, revocation_authority_par_t ra_parameters, revocation_authority_signature_t ra_signature)
{
    return 0;
}

/**
 * Sets the user attributes using the specified reader.
 *
 * @param reader the reader to be used
 * @param num_attributes the number of the user attributes
 * @return 0 if success else -1
 */
int ue_set_user_attributes(reader_t reader, size_t num_attributes)
{
    return 0;
}

/**
 * Gets the user attributes and identifier using the specified reader.
 *
 * @param reader the reader to be used
 * @param attributes the user attributes
 * @param identifier the user identifier
 * @param ra_signature the signature of the user identifier
 * @return 0 if success else -1
 */
int ue_get_user_attributes_identifier(reader_t reader, user_attributes_t *attributes, user_identifier_t *identifier, revocation_authority_signature_t *ra_signature)
{
    size_t it;

    for (it = 0; it < attributes->num_attributes; it++)
    {
        memcpy(attributes->attributes[it].value, &USER_ATTRIBUTES[it * EC_SIZE], EC_SIZE);
        attributes->attributes[it].disclosed = false;
    }

    return 0;
}

/**
 * Sets the issuer signatures of the user's attributes.
 *
 * @param reader the reader to be used
 * @param ie_parameters the issuer parameters
 * @param ie_signature the issuer signature
 * @return 0 if success else -1
 */
int ue_set_issuer_signatures(reader_t reader, issuer_par_t ie_parameters, issuer_signature_t ie_signature)
{
    return 0;
}

/**
 * Computes the proof of knowledge of the user attributes and discloses those requested
 * by the verifier.
 *
 * @param reader the reader to be used
 * @param sys_parameters the system parameters
 * @param ra_parameters the revocation authority parameters
 * @param ra_signature the signature of the user identifier
 * @param ie_signature the issuer signature
 * @param I the first pseudo-random value used to select the first randomizer
 * @param II the second pseudo-random value used to select the second randomizer
 * @param nonce the nonce generated by the verifier
 * @param nonce_length the length of the nonce
 * @param epoch the epoch generated by the verifier
 * @param epoch_length the length of the epoch
 * @param attributes the user attributes
 * @param num_disclosed_attributes the number of attributes the verifier wants to disclose
 * @param credential the credential struct to be computed by the user
 * @param pi the pi struct to be computed by the user
 * @return 0 if success else -1
 */
int ue_compute_proof_of_knowledge(reader_t reader, system_par_t sys_parameters, revocation_authority_par_t ra_parameters, revocation_authority_signature_t ra_signature,
                                  issuer_signature_t ie_signature, uint8_t I, uint8_t II, const void *nonce, size_t nonce_length, const void *epoch, size_t epoch_length,
                                  user_attributes_t *attributes, size_t num_disclosed_attributes, user_credential_t *credential, user_pi_t *pi)
{
    mclBnFr number_one, attribute;

    mclBnFr add_result, mul_result;
    mclBnFr sub_result, div_result;
    mclBnG1 add_result_g1, mul_result_g1;

    mclBnFr i;

    mclBnFr e1, e2; // e1, e2
    mclBnFr neg_e1, neg_e2; // -e1, -e2
    mclBnG1 sigma_e1, sigma_e2; // sigma_e1, sigma_e2

    mclBnFr rho, rho_v, rho_i, rho_mr;
    mclBnFr rho_e1, rho_e2;
    mclBnFr rho_mz[USER_MAX_NUM_ATTRIBUTES]; // rho non-disclosed attributes

    mclBnG1 t_verify, t_revoke;
    mclBnG1 t_sig, t_sig1, t_sig2;

    mclBnFr fr_hash;

    /*
     * IMPORTANT!
     *
     * The attributes are disclosed from the end to the beginning,
     * i.e., if a user has 4 attributes and the verifier wants to
     * disclose 2, the disclosed attributes will be the 3rd and 4th,
     * keeping hidden the 1st and 2nd.
     *
     * +---+---+---+---+
     * | 1 | 2 | 3 | 4 |
     * +---+---+---+---+
     * | H | H | D | D |
     * +---+---+---+---+
     */
    size_t num_non_disclosed_attributes;

    /*
     * IMPORTANT!
     *
     * We are using SHA1 on the Smart Card. However, because the length
     * of the SHA1 hash is 20 and the size of Fr is 32, it is necessary
     * to enlarge 12 characters and fill them with 0's.
     */
    unsigned char hash[SHA_DIGEST_PADDING + SHA_DIGEST_LENGTH] = {0};
    SHA_CTX ctx;

    size_t it;
    int r;

    if (nonce == NULL || nonce_length == 0 || epoch == NULL || epoch_length == 0 || attributes == NULL || pi == NULL || credential == NULL)
    {
        return -1;
    }

    if (attributes->num_attributes == 0 || attributes->num_attributes > USER_MAX_NUM_ATTRIBUTES || attributes->num_attributes < num_disclosed_attributes)
    {
        return -1;
    }

    // e1, e2
    memcpy(&e1, &ra_parameters.randomizers[I], sizeof(mclBnFr));
    memcpy(&e2, &ra_parameters.randomizers[II], sizeof(mclBnFr));
    // sigma_e1, sigma_e2
    memcpy(&sigma_e1, &ra_parameters.randomizers_sigma[I], sizeof(mclBnG1));
    memcpy(&sigma_e2, &ra_parameters.randomizers_sigma[II], sizeof(mclBnG1));

    /// disclose attributes
    num_non_disclosed_attributes = attributes->num_attributes - num_disclosed_attributes;

    /*
     * IMPORTANT!
     *
     * The attributes are disclosed from the end to the beginning,
     * i.e., if a user has 4 attributes and the verifier wants to
     * disclose 2, the disclosed attributes will be the 3rd and 4th,
     * keeping hidden the 1st and 2nd.
     *
     * +---+---+---+---+
     * | 1 | 2 | 3 | 4 |
     * +---+---+---+---+
     * | H | H | D | D |
     * +---+---+---+---+
     */
    for (it = num_non_disclosed_attributes; it < attributes->num_attributes; it++)
    {
        attributes->attributes[it].disclosed = true;
    }

    /// i = alpha1·e1 + alpha2·e2
    mclBnFr_mul(&i, &ra_parameters.alphas[0], &e1); // i = alpha1·e1
    mclBnFr_mul(&mul_result, &ra_parameters.alphas[1], &e2); // mul_result = alpha2·e2
    mclBnFr_add(&i, &i, &mul_result); // i = i + mul_result
    r = mclBnFr_isValid(&i);
    if (r != 1)
    {
        return -1;
    }

    // H(epoch)
    SHA1(epoch, epoch_length, &hash[SHA_DIGEST_PADDING]);

    /*
     * IMPORTANT!
     *
     * We are using SHA1 on the Smart Card. However, because the length
     * of the SHA1 hash is 20 and the size of Fr is 32, it is necessary
     * to enlarge 12 characters and fill them with 0's.
     */
    mcl_bytes_to_Fr(&fr_hash, hash, EC_SIZE);
    r = mclBnFr_isValid(&fr_hash);
    if (r != 1)
    {
        return -1;
    }

    // set 1 to Fr data type
    mclBnFr_setInt32(&number_one, 1);

    /// C = (1 / i - mr + H(epoch)) * G1
    mclBnFr_sub(&sub_result, &i, &ra_signature.mr); // sub_result = i - mr
    mclBnFr_add(&add_result, &sub_result, &fr_hash); // add_result = sub_result + H(epoch)
    mclBnFr_div(&div_result, &number_one, &add_result); // div_result = 1 / sub_result
    mclBnG1_mul(&credential->pseudonym, &sys_parameters.G1, &div_result); // pseudonym = G1 * div_result
    mclBnG1_normalize(&credential->pseudonym, &credential->pseudonym);
    r = mclBnG1_isValid(&credential->pseudonym);
    if (r != 1)
    {
        return -1;
    }

    /// rho random numbers
    // rho
    mclBnFr_setByCSPRNG(&rho);
    r = mclBnFr_isValid(&rho);
    if (r != 1)
    {
        return -1;
    }

    // rho_v
    mclBnFr_setByCSPRNG(&rho_v);
    r = mclBnFr_isValid(&rho_v);
    if (r != 1)
    {
        return -1;
    }

    // rho_i
    mclBnFr_setByCSPRNG(&rho_i);
    r = mclBnFr_isValid(&rho_i);
    if (r != 1)
    {
        return -1;
    }

    // rho_mr
    mclBnFr_setByCSPRNG(&rho_mr);
    r = mclBnFr_isValid(&rho_mr);
    if (r != 1)
    {
        return -1;
    }

    // rho_mz non-disclosed attributes
    for (it = 0; it < attributes->num_attributes; it++)
    {
        if (attributes->attributes[it].disclosed == false)
        {
            mclBnFr_setByCSPRNG(&rho_mz[it]);
            r = mclBnFr_isValid(&rho_mz[it]);
            if (r != 1)
            {
                return -1;
            }
        }
    }

    // rho_e1
    mclBnFr_setByCSPRNG(&rho_e1);
    r = mclBnFr_isValid(&rho_e1);
    if (r != 1)
    {
        return -1;
    }

    // rho_e2
    mclBnFr_setByCSPRNG(&rho_e2);
    r = mclBnFr_isValid(&rho_e2);
    if (r != 1)
    {
        return -1;
    }

    /// signatures
    // sigma_hat
    mclBnG1_mul(&credential->sigma_hat, &ie_signature.sigma, &rho);
    mclBnG1_normalize(&credential->sigma_hat, &credential->sigma_hat);
    r = mclBnG1_isValid(&credential->sigma_hat);
    if (r != 1)
    {
        return -1;
    }

    // sigma_hat_e1
    mclBnG1_mul(&credential->sigma_hat_e1, &sigma_e1, &rho);
    mclBnG1_normalize(&credential->sigma_hat_e1, &credential->sigma_hat_e1);
    r = mclBnG1_isValid(&credential->sigma_hat_e1);
    if (r != 1)
    {
        return -1;
    }

    // sigma_hat_e2
    mclBnG1_mul(&credential->sigma_hat_e2, &sigma_e2, &rho);
    mclBnG1_normalize(&credential->sigma_hat_e2, &credential->sigma_hat_e2);
    r = mclBnG1_isValid(&credential->sigma_hat_e2);
    if (r != 1)
    {
        return -1;
    }

    // sigma_minus_e1
    mclBnFr_neg(&neg_e1, &e1); // neg_e1 = -e1
    mclBnG1_mul(&credential->sigma_minus_e1, &credential->sigma_hat_e1, &neg_e1); // sigma_minus_e1 = sigma_hat_e1·neg_e1
    mclBnG1_mul(&mul_result_g1, &sys_parameters.G1, &rho); // mul_result_g1 = G1·rho
    mclBnG1_add(&credential->sigma_minus_e1, &credential->sigma_minus_e1, &mul_result_g1);  // sigma_minus_e1 = sigma_minus_e1 + mul_result_g1
    mclBnG1_normalize(&credential->sigma_minus_e1, &credential->sigma_minus_e1);
    r = mclBnG1_isValid(&credential->sigma_minus_e1);
    if (r != 1)
    {
        return -1;
    }

    // sigma_minus_e2
    mclBnFr_neg(&neg_e2, &e2); // neg_e2 = -e2
    mclBnG1_mul(&credential->sigma_minus_e2, &credential->sigma_hat_e2, &neg_e2); // sigma_minus_e2 = sigma_hat_e2·neg_e2
    mclBnG1_mul(&mul_result_g1, &sys_parameters.G1, &rho); // mul_result_g1 = G1·rho
    mclBnG1_add(&credential->sigma_minus_e2, &credential->sigma_minus_e2, &mul_result_g1);  // sigma_minus_e2 = sigma_minus_e2 + mul_result_g1
    mclBnG1_normalize(&credential->sigma_minus_e2, &credential->sigma_minus_e2);
    r = mclBnG1_isValid(&credential->sigma_minus_e2);
    if (r != 1)
    {
        return -1;
    }

    /// t values
    // t_verify
    mclBnG1_mul(&t_verify, &sys_parameters.G1, &rho_v); // t_verify = G1·rho_v
    mclBnFr_mul(&mul_result, &rho_mr, &rho); // mul_result = rho_mr·rho
    mclBnG1_mul(&mul_result_g1, &ie_signature.revocation_sigma, &mul_result); // mul_result_g1 = revocation_sigma·mul_result
    mclBnG1_add(&t_verify, &t_verify, &mul_result_g1); // t_verify = t_verify + mul_result_g1

    mclBnG1_clear(&add_result_g1); // add_result_g1 = 0
    for (it = 0; it < attributes->num_attributes; it++)
    {
        if (attributes->attributes[it].disclosed == false)
        {
            mclBnG1_mul(&mul_result_g1, &ie_signature.attribute_sigmas[it], &rho_mz[it]); // mul_result_g1 = sigma_x(it)·rho_mz(it)
            mclBnG1_add(&add_result_g1, &add_result_g1, &mul_result_g1); // add_result_g1 = add_result_g1 + mul_result_g1
        }
    }
    mclBnG1_mul(&mul_result_g1, &add_result_g1, &rho); // mul_result_g1 = add_result_g1·rho
    mclBnG1_add(&t_verify, &t_verify, &mul_result_g1); // t_verify = t_verify + mul_result_g1

    mclBnG1_normalize(&t_verify, &t_verify);
    r = mclBnG1_isValid(&t_verify);
    if (r != 1)
    {
        return -1;
    }

    // t_revoke
    mclBnG1_mul(&t_revoke, &credential->pseudonym, &rho_mr); // t_revoke = C·rho_mr
    mclBnG1_mul(&mul_result_g1, &credential->pseudonym, &rho_i); // mul_result_g1 = C·rho_i
    mclBnG1_add(&t_revoke, &t_revoke, &mul_result_g1); // t_revoke = t_revoke + mul_result_g1
    mclBnG1_normalize(&t_revoke, &t_revoke);
    r = mclBnG1_isValid(&t_revoke);
    if (r != 1)
    {
        return -1;
    }

    // t_sig
    mclBnG1_mul(&t_sig, &sys_parameters.G1, &rho_i); // t_sig = G1·rho_i
    mclBnG1_mul(&mul_result_g1, &ra_parameters.alphas_mul[0], &rho_e1); // mul_result_g1 = h1·rho_e1
    mclBnG1_add(&t_sig, &t_sig, &mul_result_g1); // t_sig = t_sig + mul_result_g1 (G1·rho_i + h1·rho_e1)
    mclBnG1_mul(&mul_result_g1, &ra_parameters.alphas_mul[1], &rho_e2); // mul_result_g1 = h2·rho_e2
    mclBnG1_add(&t_sig, &t_sig, &mul_result_g1); // t_sig = t_sig + mul_result_g1 (G1·rho_i + h1·rho_e1 + h2·rho_e2)
    mclBnG1_normalize(&t_sig, &t_sig);
    r = mclBnG1_isValid(&t_sig);
    if (r != 1)
    {
        return -1;
    }

    // t_sig1
    mclBnG1_mul(&t_sig1, &sys_parameters.G1, &rho_v); // t_sig1 = G1·rho_v
    mclBnG1_mul(&mul_result_g1, &credential->sigma_hat_e1, &rho_e1); // mul_result_g1 = sigma_hat_e1·rho_e1
    mclBnG1_add(&t_sig1, &t_sig1, &mul_result_g1); // t_sig1 = t_sig1 + mul_result_g1
    mclBnG1_normalize(&t_sig1, &t_sig1);
    r = mclBnG1_isValid(&t_sig1);
    if (r != 1)
    {
        return -1;
    }

    // t_sig2
    mclBnG1_mul(&t_sig2, &sys_parameters.G1, &rho_v); // t_sig2 = G1·rho_v
    mclBnG1_mul(&mul_result_g1, &credential->sigma_hat_e2, &rho_e2); // mul_result_g1 = sigma_hat_e2·rho_e2
    mclBnG1_add(&t_sig2, &t_sig2, &mul_result_g1); // t_sig2 = t_sig2 + mul_result_g1
    mclBnG1_normalize(&t_sig2, &t_sig2);
    r = mclBnG1_isValid(&t_sig2);
    if (r != 1)
    {
        return -1;
    }

#ifndef NDEBUG
    mcl_display_G1("t_verify", t_verify);
    mcl_display_G1("t_revoke", t_revoke);
    mcl_display_G1("t_sig", t_sig);
    mcl_display_G1("t_sig1", t_sig1);
    mcl_display_G1("t_sig2", t_sig2);
    mcl_display_G1("sigma_hat", credential->sigma_hat);
    mcl_display_G1("sigma_hat_e1", credential->sigma_hat_e1);
    mcl_display_G1("sigma_hat_e2", credential->sigma_hat_e2);
    mcl_display_G1("sigma_minus_e1", credential->sigma_minus_e1);
    mcl_display_G1("sigma_minus_e2", credential->sigma_minus_e2);
    mcl_display_G1("pseudonym", credential->pseudonym);
#endif

    /// e <-- H(...)
    SHA1_Init(&ctx);
    SHA1_Update(&ctx, &t_verify, sizeof(mclBnG1));
    SHA1_Update(&ctx, &t_revoke, sizeof(mclBnG1));
    SHA1_Update(&ctx, &t_sig, sizeof(mclBnG1));
    SHA1_Update(&ctx, &t_sig1, sizeof(mclBnG1));
    SHA1_Update(&ctx, &t_sig2, sizeof(mclBnG1));
    SHA1_Update(&ctx, &credential->sigma_hat, sizeof(mclBnG1));
    SHA1_Update(&ctx, &credential->sigma_hat_e1, sizeof(mclBnG1));
    SHA1_Update(&ctx, &credential->sigma_hat_e2, sizeof(mclBnG1));
    SHA1_Update(&ctx, &credential->sigma_minus_e1, sizeof(mclBnG1));
    SHA1_Update(&ctx, &credential->sigma_minus_e2, sizeof(mclBnG1));
    SHA1_Update(&ctx, &credential->pseudonym, sizeof(mclBnG1));
    SHA1_Update(&ctx, nonce, nonce_length);
    SHA1_Final(&hash[SHA_DIGEST_PADDING], &ctx);

    /*
     * IMPORTANT!
     *
     * We are using SHA1 on the Smart Card. However, because the length
     * of the SHA1 hash is 20 and the size of Fr is 32, it is necessary
     * to enlarge 12 characters and fill them with 0's.
     */
    mcl_bytes_to_Fr(&pi->e, hash, EC_SIZE);
    r = mclBnFr_isValid(&pi->e);
    if (r != 1)
    {
        return -1;
    }

#ifndef NDEBUG
    mcl_display_Fr("e", pi->e);
#endif

    /// s values
    // s_mz non-disclosed attributes
    for (it = 0; it < attributes->num_attributes; it++)
    {
        if (attributes->attributes[it].disclosed == false)
        {
            mcl_bytes_to_Fr(&attribute, attributes->attributes[it].value, EC_SIZE);
            mclBnFr_mul(&mul_result, &pi->e, &attribute); // mul_result = e·mz(it)
            mclBnFr_sub(&pi->s_mz[it], &rho_mz[it], &mul_result); // s_mz[it] = rho_mz[it] - mul_result
            r = mclBnFr_isValid(&pi->s_mz[it]);
            if (r != 1)
            {
                return -1;
            }
        }
    }

    // s_v
    mclBnFr_mul(&mul_result, &pi->e, &rho); // mul_result = e·rho
    mclBnFr_add(&pi->s_v, &rho_v, &mul_result); // s_v = rho_v + mul_result
    r = mclBnFr_isValid(&pi->s_v);
    if (r != 1)
    {
        return -1;
    }

    // s_mr
    mclBnFr_mul(&mul_result, &pi->e, &ra_signature.mr); // mul_result = e·mr
    mclBnFr_sub(&pi->s_mr, &rho_mr, &mul_result); // s_mr = rho_mr + mul_result
    r = mclBnFr_isValid(&pi->s_mr);
    if (r != 1)
    {
        return -1;
    }

    // s_i
    mclBnFr_mul(&mul_result, &pi->e, &i); // mul_result = e·i
    mclBnFr_add(&pi->s_i, &rho_i, &mul_result); // s_i = rho_i + mul_result
    r = mclBnFr_isValid(&pi->s_i);
    if (r != 1)
    {
        return -1;
    }

    // s_e1
    mclBnFr_mul(&mul_result, &pi->e, &e1); // mul_result = e·e1
    mclBnFr_sub(&pi->s_e1, &rho_e1, &mul_result); // s_e1 = rho_e1 + mul_result
    r = mclBnFr_isValid(&pi->s_e1);
    if (r != 1)
    {
        return -1;
    }

    // s_e2
    mclBnFr_mul(&mul_result, &pi->e, &e2); // mul_result = e·e2
    mclBnFr_sub(&pi->s_e2, &rho_e2, &mul_result); // s_e2 = rho_e2 + mul_result
    r = mclBnFr_isValid(&pi->s_e2);
    if (r != 1)
    {
        return -1;
    }

    return 0;
}

/**
 * Gets and displays the proof of knowledge of the user attributes.
 *
 * @param reader the reader to be used
 * @return 0 if success else -1
 */
int ue_display_proof_of_knowledge(reader_t reader)
{
    return 0;
}