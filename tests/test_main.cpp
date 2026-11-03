#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "core/models.h"
#include "core/json.h"

using namespace lite;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #cond << std::endl; \
        std::exit(1); \
    } \
} while(0)

void testAuthWaterfall() {
    Collection col;
    col.id = "col_1";
    col.name = "My API Collection";
    col.authType = AuthType::Bearer;
    col.token = "root_secret_token";

    // Level 1 folder (inherits)
    CollectionNode f1;
    f1.id = "fld_1";
    f1.name = "Users";
    f1.type = CollectionNode::Type::Folder;
    f1.authType = AuthType::Inherit;

    // Level 2 folder (overrides with Basic)
    CollectionNode f2;
    f2.id = "fld_2";
    f2.name = "Admin";
    f2.type = CollectionNode::Type::Folder;
    f2.authType = AuthType::Basic;
    f2.username = "admin_user";
    f2.password = "admin_pass";

    // Request in f2 (inherits from f2)
    CollectionNode reqAdmin;
    reqAdmin.id = "req_admin";
    reqAdmin.name = "Get Admin Stats";
    reqAdmin.type = CollectionNode::Type::Request;
    reqAdmin.authType = AuthType::Inherit;
    reqAdmin.request.name = reqAdmin.name;
    reqAdmin.request.authType = AuthType::Inherit;
    f2.children.push_back(reqAdmin);

    // Request in f1 (inherits all the way up to col)
    CollectionNode reqUser;
    reqUser.id = "req_user";
    reqUser.name = "Get Profile";
    reqUser.type = CollectionNode::Type::Request;
    reqUser.authType = AuthType::Inherit;
    reqUser.request.name = reqUser.name;
    reqUser.request.authType = AuthType::Inherit;
    f1.children.push_back(reqUser);

    f1.children.push_back(f2);
    col.children.push_back(f1);

    // Test 1: reqUser inherits Bearer from collection
    ResolvedAuth authUser = resolveEffectiveAuth(col, reqUser.id, reqUser.request);
    CHECK(authUser.type == AuthType::Bearer);
    CHECK(authUser.token == "root_secret_token");
    CHECK(authUser.sourceName.find("Collection: My API Collection") != std::string::npos);

    // Test 2: reqAdmin inherits Basic from f2
    ResolvedAuth authAdmin = resolveEffectiveAuth(col, reqAdmin.id, reqAdmin.request);
    CHECK(authAdmin.type == AuthType::Basic);
    CHECK(authAdmin.username == "admin_user");
    CHECK(authAdmin.password == "admin_pass");
    CHECK(authAdmin.sourceName.find("Folder: Admin") != std::string::npos);

    // Test 3: Request overriding directly with None
    RequestModel reqOverride;
    reqOverride.authType = AuthType::None;
    ResolvedAuth authOverride = resolveEffectiveAuth(col, reqAdmin.id, reqOverride);
    CHECK(authOverride.type == AuthType::None);

    std::cout << "[PASS] testAuthWaterfall" << std::endl;
}

void testCrossCollectionMove() {
    Collection col1;
    col1.id = "col_1";
    col1.name = "Col 1";

    CollectionNode fld1;
    fld1.id = "fld_1";
    fld1.name = "Folder 1";
    fld1.type = CollectionNode::Type::Folder;

    CollectionNode req1;
    req1.id = "req_1";
    req1.name = "Req 1";
    req1.type = CollectionNode::Type::Request;
    fld1.children.push_back(req1);
    col1.children.push_back(fld1);

    Collection col2;
    col2.id = "col_2";
    col2.name = "Col 2";

    // Move req_1 from col1/fld_1 to col2 root
    bool ok = moveNodeCrossCollection(col1, col2, "req_1", "", -1);
    CHECK(ok);
    CHECK(findNode(col1, "req_1") == nullptr);
    CHECK(findNode(col2, "req_1") != nullptr);

    // Move fld_1 from col1 into col2 under req_1 (should fail since req_1 is not a folder)
    bool failMove = moveNodeCrossCollection(col1, col2, "fld_1", "req_1", -1);
    CHECK(!failMove);

    // Move fld_1 from col1 to col2 root
    bool okFolder = moveNodeCrossCollection(col1, col2, "fld_1", "", -1);
    CHECK(okFolder);
    CHECK(findNode(col1, "fld_1") == nullptr);
    CHECK(findNode(col2, "fld_1") != nullptr);

    std::cout << "[PASS] testCrossCollectionMove" << std::endl;
}

void testLocalUrlMappingAndSecretExport() {
    Collection col;
    col.id = "col_test";
    col.name = "Export Test";
    col.variables.push_back(KeyValue{"apiKey", "super_secret_123", true, false, true});
    col.variables.push_back(KeyValue{"publicVar", "hello", true, false, false});

    // 1. Export without secrets
    Json jsonNoSecrets = collectionToJson(col, false);
    std::string dumpedNoSecrets = jsonNoSecrets.dump();
    CHECK(dumpedNoSecrets.find("super_secret_123") == std::string::npos);
    CHECK(dumpedNoSecrets.find("hello") != std::string::npos);

    // 2. Export with secrets
    Json jsonWithSecrets = collectionToJson(col, true);
    std::string dumpedWithSecrets = jsonWithSecrets.dump();
    CHECK(dumpedWithSecrets.find("super_secret_123") != std::string::npos);

    // 3. Import JSON with {{localUrl}} in request URL without variable defined
    std::string postmanJson = R"({
        "info": {
            "name": "Postman Auto LocalUrl",
            "schema": "https://schema.getpostman.com/json/collection/v2.1.0/collection.json"
        },
        "item": [
            {
                "name": "Local Request",
                "request": {
                    "method": "POST",
                    "url": {
                        "raw": "{{localUrl}}/api/test"
                    }
                }
            }
        ]
    })";

    std::string err;
    Json parsed = Json::parse(postmanJson, &err);
    CHECK(err.empty());
    Collection imported = collectionFromJson(parsed);

    // Verify localUrl is automatically mapped into collection variables
    bool foundLocalUrl = false;
    for (const auto& var : imported.variables) {
        if (var.key == "localUrl") {
            foundLocalUrl = true;
            CHECK(!var.value.empty());
            break;
        }
    }
    CHECK(foundLocalUrl);

    std::cout << "[PASS] testLocalUrlMappingAndSecretExport" << std::endl;
}

int main() {
    testAuthWaterfall();
    testCrossCollectionMove();
    testLocalUrlMappingAndSecretExport();
    std::cout << "All tests passed successfully!" << std::endl;
    return 0;
}
