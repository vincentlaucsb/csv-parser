# Test executables share an output directory. Serialize their post-build copies
# and keep Git metadata out of the debugger fixture directory.
get_filename_component(_csv_fixture_parent "${CSV_TEST_DESTINATION}" DIRECTORY)
file(MAKE_DIRECTORY "${_csv_fixture_parent}")
file(LOCK "${_csv_fixture_parent}/copy-test-data.lock" GUARD PROCESS)
file(COPY "${CSV_TEST_SOURCE}/" DESTINATION "${CSV_TEST_DESTINATION}"
    PATTERN ".git" EXCLUDE)
