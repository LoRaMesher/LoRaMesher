#ifndef _LORAMESHER_ROLE_SERVICE_H
#define _LORAMESHER_ROLE_SERVICE_H

#include "BuildOptions.h"

class RoleService {
public:
    /**
     * @brief Get the Role object
     *
     * @return Role
     */
    static uint8_t getRole();

    /**
     * @brief Set the Role object
     *
     * @param role
     */
    static void setRole(uint8_t role);

    /**
     * @brief Remove the Role object
     * 
     */
    static void removeRole(uint8_t role);

    /**
     * @brief Check if the node hast the role of the parameter
     *
     * @param role Role to be checked
     * @return true If the node has the role
     * @return false If the node not has the role
     */

    static bool isRole(uint8_t role);

    /**
     * @brief Get if the node is a gateway
     *
     * @return true If the node is a gateway
     * @return false If the node is not a gateway
     */
    static bool isGateway();

private:
    /**
     * @brief Node Role
     *
     */
    static uint8_t* nodeRole;
};

#endif