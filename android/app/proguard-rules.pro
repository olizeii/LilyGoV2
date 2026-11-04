# Anthropic-SDK bringt eigene R8-Regeln mit (META-INF/proguard). OpenTelemetry referenziert
# nur zur Compile-Zeit die AutoValue-Annotation.
-dontwarn com.google.auto.value.AutoValue
-dontwarn com.google.auto.value.AutoValue$*
