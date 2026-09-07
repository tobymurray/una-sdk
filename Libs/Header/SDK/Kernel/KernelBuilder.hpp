/**
 ******************************************************************************
 * @file    KernelBuilder.hpp
 * @date    24-September-2025
 * @author  Oleksandr Tymoshenko <oleksandr.tymoshenko@droid-technologies.com>
 * @brief   Helper to construct an SDK::Kernel facade from an IKernel provider.
 * @details Declares a lightweight builder that resolves all required subsystem
 *          interfaces via @c IKernel::queryInterface and returns an initialized
 *          @c SDK::Kernel facade. The @c require<T>() helper asserts that each
 *          requested interface is available and safely casts the returned pointer.
 *
 * @note    This header only declares the builder; the actual construction logic
 *          (e.g., the @c build() definition and how the @c IKernel instance is
 *          obtained) is expected to be provided in the corresponding source file.
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#pragma once

#include <cassert>

#include "SDK/Kernel/Kernel.hpp"

namespace SDK {


class KernelBuilder {
public:

    /**
     * @brief Build a @ref SDK::Kernel facade by querying the underlying provider.
     * @details Fetches each required sub-interface by its @ref SDK::Interface::IKIP::IntfID
     *          and binds them into a single convenience object.
     * @param k: A pointer to the @ref SDK::Interface::IKernel.
     * @return A value instance of @ref SDK::Kernel holding references to all sub-interfaces.
     *
     * @pre @c kernel is non-null and provides all interfaces used below.
     * @note The facade is returned by value; the contained references remain valid only
     *       as long as the underlying kernel provider remains alive.
     */
    static SDK::Kernel make(const SDK::Interface::IKernel* k);

    template <class T>
    static T& require(const SDK::Interface::IKernel* k, SDK::Interface::IKIP::IntfID id) {
        return *static_cast<T*>(queryOrAssert(k, id));
    }

private:
    /**
     * @brief   Query one interface and assert it exists.
     * @param   k: A pointer to the @ref SDK::Interface::IKernel.
     * @param   id: Which interface to fetch.
     * @return  The interface pointer, never null once the assert is enabled.
     * @note    Deliberately not a template, though @ref require is: the check
     *          does not depend on @c T, and @c assert expands
     *          @c __PRETTY_FUNCTION__, so holding it here emits one signature
     *          string instead of one per interface type. The five interfaces
     *          @ref make asks for carried 683 bytes of signature between them
     *          against 109 for this one, and every blob measured shed about
     *          692 bytes. To re-measure, count @c SDK::KernelBuilder strings
     *          in a linked app's @c .rodata.
     */
    static void* queryOrAssert(const SDK::Interface::IKernel* k, SDK::Interface::IKIP::IntfID id);

    KernelBuilder()  = delete;
    ~KernelBuilder() = delete;
};

} // namespace SDK
