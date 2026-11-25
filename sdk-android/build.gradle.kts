plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.triskelion.sdk"
    compileSdk = 35

    defaultConfig {
        minSdk = 24
        consumerProguardFiles("consumer-rules.pro")
        ndk { abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64") }
        externalNativeBuild { cmake { cppFlags += "-std=c++17" } }
    }

    buildFeatures { prefab = true }

    externalNativeBuild {
        cmake { path = file("src/main/cpp/CMakeLists.txt") }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}

dependencies {
    // Ed25519 + SHA-256 for the native core (the NDK ships no OpenSSL).
    // Override with -Ptriskelion.opensslVersion=<v> if this one is ever pulled from Google Maven.
    val opensslVersion = (findProperty("triskelion.opensslVersion") as String?) ?: "1.1.1q-beta-1"
    implementation("com.android.ndk.thirdparty:openssl:$opensslVersion")
    // Wake-up when the app is in the background. Needs google-services.json in the host app.
    implementation("com.google.firebase:firebase-messaging:24.1.0")
    implementation("androidx.core:core-ktx:1.15.0")
}
