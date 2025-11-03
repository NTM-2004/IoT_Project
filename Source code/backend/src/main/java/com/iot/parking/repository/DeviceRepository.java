package com.iot.parking.repository;

import java.util.Optional;

import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;
import com.iot.parking.model.Device;

@Repository
public interface DeviceRepository extends JpaRepository<Device, Long> {
	Optional<Device> findByDeviceId(String deviceId);
}