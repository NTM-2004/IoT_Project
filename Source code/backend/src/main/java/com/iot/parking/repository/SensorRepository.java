package com.iot.parking.repository;

import java.util.List;

import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;

import com.iot.parking.model.Sensor;

@Repository
public interface SensorRepository extends JpaRepository<Sensor, Long> {
	List<Sensor> findByDeviceIdOrderByTimestampDesc(String deviceId);
}