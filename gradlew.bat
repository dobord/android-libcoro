@ECHO OFF
SET DIR=%~dp0
SET GRADLE_WRAPPER_JAR=%DIR%gradle\wrapper\gradle-wrapper.jar
IF NOT EXIST "%GRADLE_WRAPPER_JAR%" (
  ECHO Gradle wrapper JAR not found. Please add Gradle wrapper distribution.
  EXIT /B 1
)
java %GRADLE_OPTS% -jar "%GRADLE_WRAPPER_JAR%" %*
