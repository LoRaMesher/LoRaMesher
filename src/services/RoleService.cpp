#include "RoleService.h"

uint8_t RoleService::getRole() {
    return *nodeRole;
}

void RoleService::setRole(uint8_t role) {
    *nodeRole = role;
}

void RoleService::removeRole(uint8_t role) {
    *nodeRole = *nodeRole & ~role;
}

bool RoleService::isGateway() {
    return isRole(ROLE_GATEWAY);
}

bool RoleService::isRole(uint8_t role) {
    return (*nodeRole & role) == role;
}

uint8_t* RoleService::nodeRole = new uint8_t(ROLE_DEFAULT);