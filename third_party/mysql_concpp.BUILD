package(default_visibility = ["//visibility:public"])

cc_library(
    name = "mysql_concpp_hdrs",
    hdrs = glob(
        [
            "include/mysqlx/**/*.h",
            "include/mysqlx/**/*.hpp",
            "include/mysql/**/*.h",
        ],
        allow_empty = True,
    ),
    includes = ["include"],
)
